/* SPDX-License-Identifier: MIT */
#include "../platform.c"
#include <assert.h>
#include <stdlib.h>
#include <unistd.h>
static void write_line(const char *path, const char *text)
{
    FILE *f = fopen(path, "w");
    assert(f && fputs(text, f) >= 0 && !fclose(f));
}
int main(void)
{
    char temp[] = "/tmp/unicorn-platform-XXXXXX", detail[192];
    assert(mkdtemp(temp) && !chdir(temp));
    assert(!mkdir("remoteproc7", 0700) && !mkdir("dev", 0700));
    assert(check(".", "dev", detail, sizeof(detail)) == -1);
    write_line("remoteproc7/name", "adsp\n");
    write_line("remoteproc7/state", "running\n");
    assert(check(".", "dev", detail, sizeof(detail)) == -1);
    write_line("remoteproc7/name", "cdsp\n");
    write_line("remoteproc7/state", "offline\n");
    assert(check(".", "dev", detail, sizeof(detail)) == -1 && strstr(detail, "offline"));
    write_line("remoteproc7/state", "running\n");
    write_line("dev/fastrpc-cdsp", "");
    assert(check(".", "dev", detail, sizeof(detail)) == -1 && strstr(detail, "FastRPC"));
    assert(!unlink("dev/fastrpc-cdsp") && !symlink("/dev/null", "dev/fastrpc-cdsp"));
    assert(!check(".", "dev", detail, sizeof(detail)));
    assert(!rename("dev/fastrpc-cdsp", "dev/fastrpc-cdsp-secure"));
    assert(!check(".", "dev", detail, sizeof(detail)));
    assert(!symlink("remoteproc7", "remoteproc8"));
    assert(check(".", "dev", detail, sizeof(detail)) == -1);
    assert(!unlink("remoteproc8") && !unlink("dev/fastrpc-cdsp-secure"));
    assert(!unlink("remoteproc7/name") && !unlink("remoteproc7/state"));
    assert(!rmdir("remoteproc7") && !rmdir("dev") && !chdir("/") && !rmdir(temp));
    puts("platform: missing/offline/ambiguous CDSP, absent/non-character FastRPC, dynamic numbering and secure node passed");
}
