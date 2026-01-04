#include <stdlib.h>
#include <string.h>
#include <dirent.h>

int sort(const struct dirent **a, const struct dirent **b) {
  const char *name1 = (*a)->d_name;
  const char *name2 = (*b)->d_name;

  int dot1 = (name1[0] == '.');
  int dot2 = (name2[0] == '.');

  if (dot1 && !dot2) return -1;
  if (!dot1 && dot2) return 1;

  return strcasecmp(name1, name2);
}
