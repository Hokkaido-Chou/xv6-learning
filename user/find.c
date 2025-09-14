// user/find.c
#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/fs.h"
#include "user.h"

// 打印路径path中最后一段名（basename）
static char*
basename(char *path) {
  char *p = path;
  for (char *s = path; *s; s++)
    if (*s == '/')
      p = s + 1;
  return p;
}

static void
do_find(char *path, char *target) {
  int fd;
  struct stat st;

  if ((fd = open(path, 0)) < 0) { /* ... */ }

  if (fstat(fd, &st) < 0) { /* ... */ }

  if (st.type == T_FILE || st.type == T_DEVICE) {
    if (strcmp(basename(path), target) == 0)
      printf("%s\n", path);
    close(fd);
    return;
  }

  if (st.type == T_DIR) {
    // ★ 目录自身名字命中也要打印
    if (strcmp(basename(path), target) == 0)
      printf("%s\n", path);

    char buf[512];
    struct dirent de;
    while (read(fd, &de, sizeof(de)) == sizeof(de)) {
      if (de.inum == 0) continue;

      char name[DIRSIZ + 1];
      memmove(name, de.name, DIRSIZ);
      name[DIRSIZ] = 0;

      if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
        continue;

      if (strlen(path) + 1 + strlen(name) + 1 > sizeof(buf)) {
        fprintf(2, "find: path too long\n");
        continue;
      }
      strcpy(buf, path);
      int n = strlen(buf);
      buf[n] = '/';
      buf[n+1] = 0;
      strcpy(buf + n + 1, name);

      do_find(buf, target);
    }
    close(fd);
  }
}


int
main(int argc, char *argv[]) {
  if (argc != 3) {
    fprintf(2, "usage: find <path> <name>\n");
    exit(1);
  }
  do_find(argv[1], argv[2]);
  exit(0);
}