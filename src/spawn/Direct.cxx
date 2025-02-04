/*
 * Copyright 2007-2021 CM4all GmbH
 * All rights reserved.
 *
 * author: Max Kellermann <mk@cm4all.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * - Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the
 * distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * FOUNDATION OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "Direct.hxx"
#include "Prepared.hxx"
#include "CgroupOptions.hxx"
#include "SeccompFilter.hxx"
#include "SyscallFilter.hxx"
#include "Init.hxx"
#include "daemon/Client.hxx"
#include "net/EasyMessage.hxx"
#include "net/UniqueSocketDescriptor.hxx"
#include "io/UniqueFileDescriptor.hxx"
#include "io/WriteFile.hxx"
#include "system/CoreScheduling.hxx"
#include "system/IOPrio.hxx"
#include "util/PrintException.hxx"
#include "util/Sanitizer.hxx"
#include "util/ScopeExit.hxx"

#ifdef HAVE_LIBSYSTEMD
#include <systemd/sd-journal.h>
#endif

#include <stdexcept>

#include <assert.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <sys/prctl.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <signal.h>
#include <sched.h>

#ifndef PR_SET_NO_NEW_PRIVS
#define PR_SET_NO_NEW_PRIVS 38
#endif

static void
CheckedDup2(FileDescriptor oldfd, FileDescriptor newfd)
{
	if (oldfd.IsDefined())
		oldfd.CheckDuplicate(newfd);
}

static void
CheckedDup2(FileDescriptor oldfd, int newfd)
{
	CheckedDup2(oldfd, FileDescriptor(newfd));
}

static void
DisconnectTty()
{
	FileDescriptor fd;
	if (fd.Open("/dev/tty", O_RDWR)) {
		(void) ioctl(fd.Get(), TIOCNOTTY, nullptr);
		fd.Close();
	}
}

static void
UnignoreSignals()
{
	/* restore all signals which were set to SIG_IGN by
	   RunSpawnServer2() and others */
	static constexpr int signals[] = {
		SIGHUP,
		SIGINT, SIGQUIT,
		SIGPIPE,
		SIGTERM,
		SIGUSR1, SIGUSR2,
		SIGCHLD,
		SIGTRAP,
	};

	for (auto i : signals)
		signal(i, SIG_DFL);
}

static void
UnblockSignals()
{
	sigset_t mask;
	sigfillset(&mask);
	sigprocmask(SIG_UNBLOCK, &mask, nullptr);
}

[[noreturn]]
static void
Exec(const char *path, PreparedChildProcess &&p,
     UniqueFileDescriptor &&userns_create_pipe_w,
     UniqueFileDescriptor &&wait_pipe_r,
     const CgroupState &cgroup_state)
try {
	UnignoreSignals();
	UnblockSignals();

	if (p.umask >= 0)
		umask(p.umask);

	TryWriteExistingFile("/proc/self/oom_score_adj",
			     p.ns.mount.pivot_root == nullptr
			     ? "700"
			     /* higher OOM score adjustment for jailed
				(per-account?) processes */
			     : "800");

	FileDescriptor stdout_fd = p.stdout_fd, stderr_fd = p.stderr_fd;
#ifdef HAVE_LIBSYSTEMD
	if (!stdout_fd.IsDefined() ||
	    (!stderr_fd.IsDefined() && p.stderr_path == nullptr)) {
		/* if no log destination was specified, log to the systemd
		   journal */
		/* note: this must be done before NamespaceOptions::Apply(),
		   because inside the new root, we don't have access to
		   /run/systemd/journal/stdout */
		int journal_fd = sd_journal_stream_fd(p.args.front(), LOG_INFO, true);
		if (!stdout_fd.IsDefined())
			stdout_fd = FileDescriptor{journal_fd};
		if (!stderr_fd.IsDefined() && p.stderr_path == nullptr)
			stderr_fd = FileDescriptor{journal_fd};
	}
#endif

	if (p.cgroup != nullptr)
		p.cgroup->Apply(cgroup_state, 0);

	if (p.ns.enable_cgroup &&
	    p.cgroup != nullptr && p.cgroup->IsDefined()) {
		/* if the process was just moved to another cgroup, we need to
		   unshare the cgroup namespace to hide our new cgroup
		   membership */

		if (unshare(CLONE_NEWCGROUP) < 0)
			throw MakeErrno("Failed to unshare cgroup namespace");
	}

	p.ns.Apply(p.uid_gid);

	if (!wait_pipe_r.IsDefined())
		/* if the wait_pipe exists, then the parent process
		   will apply the resource limits */
		p.rlimits.Apply(0);

	if (p.chroot != nullptr && chroot(p.chroot) < 0) {
		fprintf(stderr, "chroot('%s') failed: %s\n",
			p.chroot, strerror(errno));
		_exit(EXIT_FAILURE);
	}

	if (userns_create_pipe_w.IsDefined()) {
		/* user namespace allocation was postponed to allow
		   mounting /proc with a reassociated PID namespace
		   (which would not be allowed from inside a new user
		   namespace, because the user namespace drops
		   capabilities on the PID namespace) */

		assert(wait_pipe_r.IsDefined());

		if (unshare(CLONE_NEWUSER) < 0)
			throw MakeErrno("unshare(CLONE_NEWUSER) failed");

		/* after success (no exception was thrown), we send
		   one byte to the pipe and close it, so the parent
		   knows everything's ok; without that byte, it would
		   fail */
		static constexpr char zero = 0;
		userns_create_pipe_w.Write(&zero, sizeof(zero));
		userns_create_pipe_w.Close();
	}

	if (wait_pipe_r.IsDefined()) {
		/* wait for the parent to set us up */

		/* expect one byte to indicate success, and then the pipe will
		   be closed by the parent */
		char buffer;
		if (wait_pipe_r.Read(&buffer, sizeof(buffer)) != 1 ||
		    wait_pipe_r.Read(&buffer, sizeof(buffer)) != 0)
			_exit(EXIT_FAILURE);
	}

	if (p.sched_idle) {
		static struct sched_param sched_param;
		sched_setscheduler(0, SCHED_IDLE, &sched_param);
	}

	if (p.priority != 0 &&
	    setpriority(PRIO_PROCESS, getpid(), p.priority) < 0) {
		fprintf(stderr, "setpriority() failed: %s\n", strerror(errno));
		_exit(EXIT_FAILURE);
	}

	if (p.ioprio_idle)
		ioprio_set_idle();

	if (p.ns.enable_pid && p.ns.pid_namespace == nullptr) {
		setsid();

		const char *name = nullptr;
		if (p.cgroup != nullptr)
			name = p.cgroup->name;

		const auto pid = SpawnInitFork(name);
		assert(pid >= 0);

		if (pid > 0)
			_exit(SpawnInit(pid, false));
	}

	if (p.no_new_privs)
		prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0);

	try {
		Seccomp::Filter sf(SCMP_ACT_ALLOW);

#ifdef PR_SET_NO_NEW_PRIVS
		/* don't enable PR_SET_NO_NEW_PRIVS unless the feature
		   was explicitly enabled */
		if (!p.no_new_privs)
			sf.SetAttributeNoThrow(SCMP_FLTATR_CTL_NNP, 0);
#endif

		sf.AddSecondaryArchs();

		BuildSyscallFilter(sf);

		if (p.forbid_user_ns)
			ForbidUserNamespace(sf);

		if (p.forbid_multicast)
			ForbidMulticast(sf);

		if (p.forbid_bind)
			ForbidBind(sf);

		sf.Load();
	} catch (const std::runtime_error &e) {
		if (p.HasSyscallFilter())
			/* filter options have been explicitly enabled, and thus
			   failure to set up the filter are fatal */
			throw;

		fprintf(stderr, "Failed to setup seccomp filter for '%s': %s\n",
			path, e.what());
	}

	if (!p.uid_gid.IsEmpty())
		p.uid_gid.Apply();

	if (p.chdir != nullptr && chdir(p.chdir) < 0) {
		fprintf(stderr, "chdir('%s') failed: %s\n",
			p.chdir, strerror(errno));
		_exit(EXIT_FAILURE);
	}

	if (!stderr_fd.IsDefined() && p.stderr_path != nullptr) {
		if (!stderr_fd.Open(p.stderr_path,
				    O_CREAT|O_WRONLY|O_APPEND|O_CLOEXEC|O_NOCTTY,
				    0600)) {
			perror("Failed to open STDERR_PATH");
			_exit(EXIT_FAILURE);
		}
	}

	if (p.return_stderr.IsDefined()) {
		assert(stderr_fd.IsDefined());

		EasySendMessage(p.return_stderr, stderr_fd);
		p.return_stderr.Close();
	}

	constexpr int CONTROL_FILENO = 3;
	CheckedDup2(p.stdin_fd, STDIN_FILENO);
	CheckedDup2(stdout_fd, STDOUT_FILENO);
	CheckedDup2(stderr_fd, STDERR_FILENO);
	CheckedDup2(p.control_fd, CONTROL_FILENO);

	if (p.tty)
		DisconnectTty();

	if (p.session)
		setsid();

	if (p.tty) {
		assert(p.stdin_fd.IsDefined());
		assert(p.stdin_fd == p.stdout_fd);

		if (ioctl(p.stdin_fd.Get(), TIOCSCTTY, nullptr) < 0) {
			perror("Failed to set the controlling terminal");
			_exit(EXIT_FAILURE);
		}
	}

	if (p.exec_function != nullptr) {
		_exit(p.exec_function(std::move(p)));
	} else {
		execve(path, const_cast<char *const*>(&p.args.front()),
		       const_cast<char *const*>(&p.env.front()));

		fprintf(stderr, "failed to execute %s: %s\n", path, strerror(errno));
		_exit(EXIT_FAILURE);
	}
} catch (const std::exception &e) {
	PrintException(e);
	_exit(EXIT_FAILURE);
}

struct SpawnChildProcessContext {
	PreparedChildProcess &&params;
	const CgroupState &cgroup_state;

	const char *path;

	/**
	 * A pipe used by the parent process to wait for the child to
	 * create the user namespace.
	 */
	UniqueFileDescriptor userns_create_pipe_r, userns_create_pipe_w;

	/**
	 * A pipe used by the child process to wait for the parent to set
	 * it up (e.g. uid/gid mappings).
	 */
	UniqueFileDescriptor wait_pipe_r, wait_pipe_w;

	SpawnChildProcessContext(PreparedChildProcess &&_params,
				 const CgroupState &_cgroup_state) noexcept
		:params(std::move(_params)),
		 cgroup_state(_cgroup_state),
		 path(_params.Finish()) {}
};

static int
spawn_fn(void *_ctx)
{
	auto &ctx = *(SpawnChildProcessContext *)_ctx;

	ctx.userns_create_pipe_r.Close();
	ctx.wait_pipe_w.Close();

	Exec(ctx.path, std::move(ctx.params),
	     std::move(ctx.userns_create_pipe_w),
	     std::move(ctx.wait_pipe_r),
	     ctx.cgroup_state);
}

pid_t
SpawnChildProcess(PreparedChildProcess &&params,
		  const CgroupState &cgroup_state,
		  bool is_sys_admin)
{
	int clone_flags = SIGCHLD;
	clone_flags = params.ns.GetCloneFlags(clone_flags);

	if (params.cgroup != nullptr && params.cgroup->IsDefined())
		/* postpone creating the new cgroup namespace until
		   after this process has been moved to the new
		   cgroup, or else it won't have the required
		   permissions to do so, because the destination
		   cgroup won't be visible from his namespace */
		clone_flags &= ~CLONE_NEWCGROUP;

	SpawnChildProcessContext ctx(std::move(params), cgroup_state);

	UniqueFileDescriptor old_pidns;

	AtScopeExit(&old_pidns) {
		/* restore the old namespaces */
		if (old_pidns.IsDefined())
			setns(old_pidns.Get(), CLONE_NEWPID);
	};

	if (ctx.params.ns.pid_namespace != nullptr) {
		/* first open a handle to our existing (old) namespaces
		   to be able to restore them later (see above) */
		if (!old_pidns.OpenReadOnly("/proc/self/ns/pid"))
			throw MakeErrno("Failed to open current PID namespace");

		auto fd = SpawnDaemon::MakePidNamespace(SpawnDaemon::Connect(),
							ctx.params.ns.pid_namespace);
		if (setns(fd.Get(), CLONE_NEWPID) < 0)
			throw MakeErrno("setns(CLONE_NEWPID) failed");
	}

	if (ctx.params.ns.enable_user && is_sys_admin) {
		/* from inside the new user namespace, we cannot
		   reassociate with a new network namespace or mount
		   /proc of a reassociated PID namespace, because at
		   this point, we have lost capabilities on those
		   namespaces; therefore, postpone CLONE_NEWUSER until
		   everything is set up; to synchronize this, create
		   two pairs of pipes */

		if (!UniqueFileDescriptor::CreatePipe(ctx.userns_create_pipe_r,
						      ctx.userns_create_pipe_w))
			throw MakeErrno("pipe() failed");

		if (!UniqueFileDescriptor::CreatePipe(ctx.wait_pipe_r, ctx.wait_pipe_w))
			throw MakeErrno("pipe() failed");

		/* disable CLONE_NEWUSER for the clone() call, because
		   the child process will call
		   unshare(CLONE_NEWUSER) */
		clone_flags &= ~CLONE_NEWUSER;

		/* this process will set up the uid/gid maps, so
		   disable that part in the child process */
		ctx.params.ns.enable_user = false;
	}

	char stack[HaveAddressSanitizer() ? 32768 : 16384];
	long pid = clone(spawn_fn, stack + sizeof(stack), clone_flags, &ctx);
	if (pid < 0)
		throw MakeErrno("clone() failed");

	if (ctx.userns_create_pipe_r.IsDefined()) {
		/* wait for the child to create the user namespace */

		ctx.userns_create_pipe_w.Close();

		/* expect one byte to indicate success, and then the
		   pipe will be closed by the child */
		char buffer;
		if (ctx.userns_create_pipe_r.Read(&buffer, sizeof(buffer)) != 1 ||
		    ctx.userns_create_pipe_r.Read(&buffer, sizeof(buffer)) != 0)
			throw std::runtime_error("User namespace setup failed");
	}

	if (ctx.wait_pipe_w.IsDefined()) {
		/* set up the child's uid/gid mapping and wake it up */
		ctx.wait_pipe_r.Close();
		ctx.params.ns.SetupUidGidMap(ctx.params.uid_gid, pid);

		/* apply the resource limits in the parent process, because
		   the child has lost all root namespace capabilities by
		   entering a new user namespace */
		ctx.params.rlimits.Apply(pid);

		/* if this is a jailed process, we assume it's
		   unprivileged and should not share a HT core with a
		   process for a different user to avoid cross-HT
		   attacks, so create a new core scheduling cookie */
		/* failure to do so will be ignored silently, because
		   the Linux kernel may not have that feature yet */
		if (ctx.params.ns.mount.pivot_root != nullptr)
			CoreScheduling::Create(pid);

		/* after success (no exception was thrown), we send one byte
		   to the pipe and close it, so the child knows everything's
		   ok; without that byte, it would exit immediately */
		static constexpr char buffer = 0;
		ctx.wait_pipe_w.Write(&buffer, sizeof(buffer));
		ctx.wait_pipe_w.Close();
	}

	return pid;
}
