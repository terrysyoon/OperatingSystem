#include "types.h"
#include "stat.h"
#include "user.h"

int
main(int argc, char *argv[])
{
  if(argc == 4 && strcmp(argv[1], "-s") == 0){
    if(symlink(argv[2], argv[3]) < 0)
      printf(2, "symlink %s %s: failed\n", argv[2], argv[3]);
    exit();
  }

  if(argc == 3){
    if(link(argv[1], argv[2]) < 0)
      printf(2, "link %s %s: failed\n", argv[1], argv[2]);
    exit();
  }

  
  printf(2, "Usage: ln old new\n"); 
  exit();
}
