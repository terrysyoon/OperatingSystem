#include "types.h"
#include "stat.h"
#include "user.h"

int
main(int argc, char *argv[])
{
  int a;
  while((a=sync()) == -1);
  printf(1, "sync() returned %d\n", a);
  exit();
}
