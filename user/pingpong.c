// user/pingpong.c
#include "kernel/types.h"
#include "user.h"

int
main(void)
{
  int f2c[2];   // father -> child
  int c2f[2];   // child  -> father
  if (pipe(f2c) < 0 || pipe(c2f) < 0) {
    printf("pipe failed\n");
    exit(1);
  }

  int pid = fork();
  if (pid < 0) {
    printf("fork failed\n");
    exit(1);
  }

  if (pid == 0) {
    // --- child ---
    // 只读父->子，仅读端；只写子->父，仅写端
    close(f2c[1]); // 不写 f2c
    close(c2f[0]); // 不读 c2f

    int ppid = 0;
    if (read(f2c[0], &ppid, sizeof(ppid)) != sizeof(ppid)) {
      // 读失败直接退出
      exit(1);
    }
    printf("%d: received ping from pid %d\n", getpid(), ppid);

    int my = getpid();
    write(c2f[1], &my, sizeof(my));

    close(f2c[0]);
    close(c2f[1]);
    exit(0);
  } else {
    // --- parent ---
    close(f2c[0]); // 不读 f2c
    close(c2f[1]); // 不写 c2f

    int my = getpid();
    write(f2c[1], &my, sizeof(my));     // 把父 pid 发给子

    int cpid = 0;
    if (read(c2f[0], &cpid, sizeof(cpid)) != sizeof(cpid)) {
      exit(1);
    }
    printf("%d: received pong from pid %d\n", getpid(), cpid);

    close(f2c[1]);
    close(c2f[0]);
    wait(0); // 等子进程结束
    exit(0);
  }
}