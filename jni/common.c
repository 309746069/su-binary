#include "common.h"
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <sys/syscall.h>
#include <sys/mount.h>
#include <sys/wait.h>
#include <sys/un.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <stdbool.h>

#include "su.h"
#include "utils.h"

#define BUFFSIZE   4096
#define MULTIUSER_APP_PER_USER_RANGE 100000


#ifndef userid_t
#define userid_t uid_t
#endif
userid_t multiuser_get_user_id(uid_t uid) {
    return uid / MULTIUSER_APP_PER_USER_RANGE;
}



///////////////////////////////////////////////////////////////////////////////

#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>
#include <paths.h>

extern char **environ;

int
my_system(const char *command)
{
    pid_t pid, cpid;
    struct sigaction intsave, quitsave;
    sigset_t mask, omask;
    int pstat;
    char *argp[] = {"sh", "-c", NULL, NULL};

    if (!command)       /* just checking... */
        return(1);

    argp[2] = (char *)command;

    sigemptyset(&mask);
    sigaddset(&mask, SIGCHLD);
    sigprocmask(SIG_BLOCK, &mask, &omask);
    switch (cpid = vfork()) {
    case -1:            /* error */
        sigprocmask(SIG_SETMASK, &omask, NULL);
        return(-1);
    case 0:             /* child */
        sigprocmask(SIG_SETMASK, &omask, NULL);
        execve(_PATH_BSHELL, argp, environ);
        _exit(127);
    }

    sigaction(SIGINT, NULL, &intsave);
    sigaction(SIGQUIT, NULL, &quitsave);
    do {
        pid = waitpid(cpid, &pstat, 0);
    } while (pid == -1 && errno == EINTR);
    sigprocmask(SIG_SETMASK, &omask, NULL);
    sigaction(SIGINT, &intsave, NULL);
    sigaction(SIGQUIT, &quitsave, NULL);
    return (pid == -1 ? -1 : pstat);
}

int run_command(const char *fmt, ...)
{
    va_list args;
    int len;
    char *buffer;
    va_start(args, fmt);
    len = vsnprintf(NULL, 0, fmt, args) + 1;
    buffer = calloc(1, len);
    vsnprintf(buffer, len, fmt, args);
    va_end(args);

    int ret = -1;

    if (access(_PATH_BSHELL, X_OK) == 0) {
        ret = my_system(buffer);
    } else {
        ret = system(buffer);
    }

    free(buffer);
    return ret;
}

///////////////////////////////////////////////////////////////////////////////



int file_exists(const char *path) {
    return access(path, R_OK) == 0;
}

int setxattr(const char *path, const char *value) {
    if (!file_exists("/sys/fs/selinux")) {
        return 0;
    }
    return syscall(__NR_setxattr, path, "security.selinux", value, strlen(value), 0);
}

int selinux_attr_set_priv() {
    int fd, n;
    fd = open("/proc/self/attr/current", O_WRONLY);
    if (fd < 0) {
        return -1;
    }
    n = write(fd, "u:r:init:s0\n", 12);
    close(fd);
    return n == 12 ? 0 : -1;
}

int copy_file(const char *src_file, const char *dst_file) {
    int from_fd, to_fd;
    int n;
    char buf[BUFFSIZE];

    from_fd = open(src_file, O_RDONLY);
    if (from_fd < 0) {
        return -1;
    }

    to_fd = open(dst_file, O_WRONLY|O_CREAT|O_TRUNC, 0777);
    if (to_fd < 0) {
        close(from_fd);
        return -1;
    }

    while ((n = read(from_fd, buf, BUFFSIZE)) > 0) {
        if (write(to_fd, buf, n) != n) {
            break;
        }
    }

    close(from_fd);
    close(to_fd);

    return n == 0 ? 0 : -1;
}



////////////////////////////////////////////////////////////////////////////////


static int write_file(const char *path, const char *data, uid_t owner, gid_t group, mode_t mode) {
    int fd = open(path, O_WRONLY|O_CREAT|O_TRUNC, mode);
    if (fd < 0) {
        return -1;
    }
    int len = strlen(data);
    int n = write(fd, data, len);
    close(fd);
    chown(path, owner, group);
    chmod(path, mode);
    return n == len ? 0 : -1;;
}

char *format_string(const char *fmt, ...)
{
    va_list args;
    int     len;
    char    *buffer;
    va_start(args, fmt);
    len = vsnprintf(NULL, 0, fmt, args) + 1;
    buffer = calloc(1, len);
    vsnprintf(buffer, len, fmt, args);
    va_end(args);
    return buffer;
}

void exec_log(const char *priorityChar, const char *tag, const char *message) {

    /*
        USAGE: /system/bin/log [-p priorityChar] [-t tag] message
            priorityChar should be one of:
                v,d,i,w,e
    */

    int pid;
    if ((pid = fork()) == 0) {
        int null = open("/dev/null", O_WRONLY | O_CLOEXEC);
        dup2(null, STDIN_FILENO);
        dup2(null, STDOUT_FILENO);
        dup2(null, STDERR_FILENO);
        execl("/system/bin/log", "/system/bin/log", "-p", priorityChar, "-t", tag, message, NULL);
        _exit(0);
    }
    int status;
    waitpid(pid, &status, 0);
}

int tolog(const char* fmt, ...) {
    va_list args;
    int     len;
    char    *buffer;
    va_start( args, fmt );
    len = vsnprintf( NULL, 0, fmt, args ) + 1;
    buffer = (char*)malloc( len );
    vsnprintf( buffer, len, fmt, args );
    va_end( args );

    exec_log("d", "su-binary", buffer);

    free(buffer);
    return 0;
}

bool daemon_exists()
{
    FILE *fp_unix;
    int Num;
    int RefCount;
    int Protocol;
    int Flags;
    unsigned int Type;
    int St;
    unsigned Inode;
    char Path[PATH_MAX];

    fp_unix = fopen("/proc/net/unix", "r");
    if (!fp_unix)
        goto out;

    fgets(Path, PATH_MAX, fp_unix);
    memset(Path, 0, PATH_MAX);

    while (fscanf(fp_unix, "%x: %x %x %x %x %x %d %s\n", &Num, &RefCount, &Protocol, &Flags, &Type, &St, &Inode, Path) > 0) {
        if (strstr(Path, DAEMON_SOCKET_NAME)) {
            fclose(fp_unix);
            fprintf(stderr, "[-] Unable to start daemon : daemon is running\n");
            return  true;
        }
    }

    fclose(fp_unix);
out:
    return false;
}
