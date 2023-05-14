/*
* Date of Creation: May 14th 2023
* Author: Terry Yoon
* yoonsb@hanyang.ac.kr

* This is a user program "pmanager"
*/

/*
list
    • 현재 실행 중인 프로세스들의 정보를 출력합니다.
    • 각 프로세스의 이름, pid, 스택용 페이지의 개수, 할당받은 메모리의 크기, 메모리의 최대 제한이 있습니다.
    • Process가 실행한 Thread의 정보를 고려하여 출력하여야 합니다.
    • Thread의 경우 출력하지 않습니다.
    • 프로세스의 정보를 얻기 위한 시스템 콜은 자유롭게 정의해도 됩니다.

kill
    • pid를 가진 프로세스를 kill합니다. kill 시스템 콜을 사용하면 됩니다.
    • 성공 여부를 출력합니다.

execute <path> <stacksize>
    • path의 경로에 위치한 프로그램을 stacksize 개수만큼의 스택용 페이지와 함께 실행하게
    합니다.
    • 프로그램에 넘겨주는 인자는 하나로, 0번 인자에 path를 넣어줍니다.
    • 실행한 프로그램의 종료를 기다리지 않고 pmanager는 이어서 실행되어야 합니다.
    • 성공 시 별도의 메시지를 출력하지 않으며, 실패 시에만 메시지를 출력합니다.

memlim <pid> <limit>
    • pid를 가진 프로세스의 메모리 제한을 limit으로 설정합니다.
    • limit의 값은 0 이상의 정수이며, 양수인 경우 그 크기만큼의 limit을 가지이고 0인 경우 제한이 없습니다.
    • 프로세스의 메모리는 thread의 메모리를 고려해야 합니다.
    • 성공 여부를 출력합니다.

exit
    • pmanager를 종료합니다.

• 명령줄의 입력은 다음과 같은 형식으로 들어와야 합니다.
• 모든 입력은 알파벳 대소문자, 숫자, 공백, 그리고 개행으로만 이루어집니다.
• 명령의 맨 앞이나 맨 뒤에는 공백이 없습니다.
• 명령과 옵션, 옵션과 옵션 사이에는 정확히 하나의 공백이 주어집니다.
• pid, stacksize, limit은 0 이상 10억 이하의 정수입니다.
• path는 길이가 50을 넘지 않으며, 알파벳 대소문자와 숫자로만 이루어진 문자열입니다.
• 널문자등이들어갈공간을확보하기위해배열의크기는넉넉하게잡을것을 권장합니다.
• 각 명령은 해당 명령의 형식을 항상 따릅니다.
• 명세에 주어지지 않은 명령은 실행되지 않습니다.
*/
#include "types.h"
#include "user.h"

#define MAX_INPUT_STRING (400)

int main(int argc, char *argv[])
{
    char buf[MAX_INPUT_STRING];
    int nbuf = MAX_INPUT_STRING;

    while(1) {
        printf(1, "pmanager> ");
        memset(buf, 0, nbuf);
        gets(buf, nbuf);
        if(buf[0] == 0) continue; //EOF
        printf(1, "%s", buf);
        if(strcmp(buf, "list\n") == 0 || strcmp(buf, "list") == 0) {
            //list
            printf(1, "Name PID State NumberOfStackPages Size Limit\n");
            pmanagerList();
            printf(1, "End of List\n");
        }
        else if(strncmp(buf, "kill", strlen("kill")) == 0) { //prefix = kill 
            //kill
            if(buf[strlen("kill")] != ' ') { //kill 뒤에 공백이 아니면
                printf(1, "Usage: kill <pid>\n");
                continue;
            }
            int i,j;
            char pid_buf[MAX_INPUT_STRING];
            for(i = 0, j = strlen("kill") + 1; j < MAX_INPUT_STRING && buf[j] >= '0' && buf[j] <= '9'; i++, j++) {
                pid_buf[i] = buf[j];
            }
            pid_buf[++i] = 0;
            int pid = atoi(pid_buf);
            printf(1, "Killing target PID: %d...\n", pid);
            //kill
            if(!kill(pid)){
                printf(1, "Done!\n");
            } else {
                printf(1, "Failed! No process with PID:%d found.\n", pid);
            }
        }
        //To-do: implement execute here
        else if(strncmp(buf, "memlim", strlen("memlim")) == 0) { //prefix = memlim
            if(buf[strlen("memlim")] != ' ') { //kill 뒤에 공백이 아니면
                printf(1, "Usage: memlim <pid> <limit>\n");
                continue;
            }
            int i,j;
            char pid_buf[MAX_INPUT_STRING];
            for(i = 0, j = strlen("memlim") + 1; j < MAX_INPUT_STRING && buf[j] >= '0' && buf[j] <= '9'; i++, j++) {
                pid_buf[i] = buf[j];
            }
            pid_buf[++i] = 0;
            int pid = atoi(pid_buf);
            char limit_buf[MAX_INPUT_STRING];
            for(i = 0, j++; j < MAX_INPUT_STRING && buf[j] >= '0' && buf[j] <= '9'; i++, j++) {
                limit_buf[i] = buf[j];
            }
            limit_buf[++i] = 0;
            uint limit = atoi(limit_buf);
            printf(1, "Set memory limit>> PID: %d Limit: %d\n", pid, limit);
            if(!setmemorylimit(pid,limit)) {
                printf(1, "Success!\n");
            }
            else {
                printf(1, "Fail!\n");
            }
        }
        else if(strcmp(buf, "exit\n") == 0 || strcmp(buf, "exit") == 0) { // 개행 문자까지 들어와서, 전자에서 조건 충족.
            break;
        } 
        else {
            printf(1, "Failed to parse the command!\n");
        }
    }
    exit();
}