/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "UidGid.hxx"
#include "system/Error.hxx"
#include "util/RuntimeError.hxx"

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <pwd.h>
#include <grp.h>

void
UidGid::Lookup(const char *username)
{
    errno = 0;
    const auto *pw = getpwnam(username);
    if (pw == nullptr) {
        if (errno == 0 || errno == ENOENT)
            throw FormatRuntimeError("No such user: %s", username);
        else
            throw FormatErrno("Failed to look up user '%s'", username);
    }

    uid = pw->pw_uid;
    gid = pw->pw_gid;

    int ngroups = groups.size();
    int n = getgrouplist(username, pw->pw_gid, &groups.front(), &ngroups);
    if (n >= 0 && (size_t)n < groups.size())
        groups[n] = 0;
}

void
UidGid::LoadEffective()
{
    uid = geteuid();
    gid = getegid();
}

char *
UidGid::MakeId(char *p) const
{
    if (uid != 0)
        p += sprintf(p, ";uid%d", int(uid));

    if (gid != 0)
        p += sprintf(p, ";gid%d", int(gid));

    return p;
}

static bool
IsUid(uid_t uid)
{
    uid_t ruid, euid, suid;
    return getresuid(&ruid, &euid, &suid) == 0 &&
        uid == ruid && uid == euid && uid == suid;
}

static bool
IsGid(gid_t gid)
{
    gid_t rgid, egid, sgid;
    return getresgid(&rgid, &egid, &sgid) == 0 &&
        gid == rgid && gid == egid && gid == sgid;
}

void
UidGid::Apply() const
{
    if ((uid == 0 || IsUid(uid)) &&
        (gid == 0 || IsGid(gid)))
        /* skip if we're already the configured (unprivileged)
           uid/gid; also don't try setgroups(), because that will fail
           anyway if we're unprivileged; unprivileged operation is
           only for debugging anyway, so that's ok */
        return;

    if (gid != 0 && setregid(gid, gid) < 0)
        throw FormatErrno("setgid(%d) failed", int(gid));

    if (HasGroups()) {
        if (setgroups(CountGroups(), &groups.front()) < 0)
            throw MakeErrno("setgroups() failed");
    } else if (gid != 0) {
        if (setgroups(0, &gid) < 0)
            throw FormatErrno("setgroups(%d) failed", int(gid));
    }

    if (uid != 0 && setreuid(uid, uid) < 0)
        throw FormatErrno("setuid(%d) failed", int(uid));
}
