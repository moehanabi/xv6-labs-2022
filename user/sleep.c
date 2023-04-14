#include "kernel/types.h"
#include "user/user.h"

void sleep_u(int delay){
  sleep(delay);
}

int
main(int argc, char *argv[])
{
  if(argc == 2){
    sleep_u(atoi(argv[1]));
  }else{
    printf("Usage: sleep n(s)\n");
  }
  exit(0);
}
