/* 
 * msh - A mini shell program with job control
 * 
 * <Put your name and login ID here>
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>
#include "util.h"
#include "jobs.h"


/* Global variables */
int verbose = 0;            /* if true, print additional output */

extern char **environ;      /* defined in libc */
static char prompt[] = "msh> ";    /* command line prompt (DO NOT CHANGE) */
static struct job_t jobs[MAXJOBS]; /* The job list */
/* End global variables */


/* Function prototypes */

/* Here are the functions that you will implement */
void eval(char *cmdline);
int builtin_cmd(char **argv);
void do_bgfg(char **argv);
void waitfg(pid_t pid);

void sigchld_handler(int sig);
void sigtstp_handler(int sig);
void sigint_handler(int sig);

/* Here are helper routines that we've provided for you */
void usage(void);
void sigquit_handler(int sig);



/*
 * main - The shell's main routine 
 */
int main(int argc, char **argv) 
{
    char c;
    char cmdline[MAXLINE];
    int emit_prompt = 1; /* emit prompt (default) */

    /* Redirect stderr to stdout (so that driver will get all output
     * on the pipe connected to stdout) */
    dup2(1, 2);

    /* Parse the command line */
    while ((c = getopt(argc, argv, "hvp")) != EOF) {
        switch (c) {
        case 'h':             /* print help message */
            usage();
	    break;
        case 'v':             /* emit additional diagnostic info */
            verbose = 1;
	    break;
        case 'p':             /* don't print a prompt */
            emit_prompt = 0;  /* handy for automatic testing */
	    break;
	default:
            usage();
	}
    }

    /* Install the signal handlers */

    /* These are the ones you will need to implement */
    Signal(SIGINT,  sigint_handler);   /* ctrl-c */
    Signal(SIGTSTP, sigtstp_handler);  /* ctrl-z */
    Signal(SIGCHLD, sigchld_handler);  /* Terminated or stopped child */

    /* This one provides a clean way to kill the shell */
    Signal(SIGQUIT, sigquit_handler); 

    /* Initialize the job list */
    initjobs(jobs);

    /* Execute the shell's read/eval loop */
    while (1) {

	/* Read command line */
	if (emit_prompt) {
	    printf("%s", prompt);
	    fflush(stdout);
	}
	if ((fgets(cmdline, MAXLINE, stdin) == NULL) && ferror(stdin))
	    app_error("fgets error");
	if (feof(stdin)) { /* End of file (ctrl-d) */
	    fflush(stdout);
	    exit(0);
	}

	/* Evaluate the command line */
	eval(cmdline);
	fflush(stdout);
	fflush(stdout);
    } 

    exit(0); /* control never reaches here */
}
  
/* 
 * eval - Evaluate the command line that the user has just typed in
 * 
 * If the user has requested a built-in command (quit, jobs, bg or fg)
 * then execute it immediately. Otherwise, fork a child process and
 * run the job in the context of the child. If the job is running in
 * the foreground, wait for it to terminate and then return.  Note:
 * each child process must have a unique process group ID so that our
 * background children don't receive SIGINT (SIGTSTP) from the kernel
 * when we type ctrl-c (ctrl-z) at the keyboard.  
 * some codes are copied from H&O pg. 735,757
*/
void eval(char *cmdline) 
{
    char *argv[MAXARGS];
    char buf[MAXLINE];
    int bg;
    pid_t pid;
    sigset_t mask;

    strcpy(buf, cmdline);
    bg = parseline(buf, argv);
    if(argv[0] == NULL)
        return;

    if(!builtin_cmd(argv))
        sigemptyset(&mask);
        sigaddset(&mask, SIGCHLD);
        sigprocmask(SIG_BLOCK, &mask, NULL); // Block SIGCHLD
        // child process
        if(((pid = fork()) == 0)){
            sigprocmask(SIG_UNBLOCK, &mask,NULL); // Unblock SIGCHLD
            setpgid(0,0);
            if(execve(argv[0], argv, environ) < 0){
                printf("%s: Command not found.\n", argv[0]);
                exit(0);
            }
        }
        //parent process in foreground
        if(!bg){
            int status;
            addjob(jobs,pid,FG,cmdline);
            sigprocmask(SIG_UNBLOCK, &mask, NULL);
            if(waitpid(pid, &status, 0) < 0){}
                unix_error("waitfg: waipid error");

        }
        //process in background
        else{
            addjob(jobs,pid,BG,cmdline);
            printf("Background process: %d %s", pid, cmdline);
        }

    return;
}


/* 
 * builtin_cmd - If the user has typed a built-in command then execute
 *    it immediately.  
 * Return 1 if a builtin command was executed; return 0
 * if the argument passed in is *not* a builtin command.
 */
int builtin_cmd(char **argv) 
{
    if (!strcmp(argv[0], "quit")) /* quit command */
        exit(0);
    if (!strcmp(argv[0], "bg")){  /* background command */
        do_bgfg(argv);
        return 1;
    }  
    if (!strcmp(argv[0], "fg")){  /* foreground command */
        do_bgfg(argv);
        return 1;
    }  
    if (!strcmp(argv[0], "jobs")){ /* quit command */
        listjobs(jobs);
        return 1;
    }
    if (!strcmp(argv[0], "&"))   /* ignore singleton */
        return 1; 

    return 0;     /* not a builtin command */
}

/* 
 * do_bgfg - Execute the builtin bg and fg commands
 */
void do_bgfg(char **argv) 
{
    if(argv[1] != NULL){
        if(strcmp(argv[0], "bg") != 0 ||strcmp(argv[0], "fg") != 0){
            printf("Wrong argument\n");
            return;        
        }       

        else if(strcmp(argv[0],"bg") != 0){
            //jid
            if(argv[1][0] == '%'){
                //parse jid
                int jid = atoi(&argv[1][1]);
                getjobpid(jobs,jid) -> state = BG;   
                pid_t pid = getjobjid(jobs, jid)->pid;
                printf("[%d] (%d) %s", jid, pid, getjobjid(jobs, jid)->cmdline);
                kill(-pid, SIGCONT);                             
            }
            // pid
            else{ 
                int pid = atoi(argv[1]);
                getjobpid(jobs,pid) -> state = BG; 
                printf("[%d] (%d) %s", pid2jid(jobs, pid), pid, getjobpid(jobs, pid)->cmdline);
                kill(-pid,SIGCONT);               
            }
        }
        else{
            if(argv[1][0] == '%'){
                //parse jid
                int jid = atoi(&argv[1][1]);
                getjobpid(jobs,jid) -> state = FG; 
                pid_t pid = getjobjid(jobs, jid)->pid;  
                kill(-pid, SIGCONT);  
                waitfg(pid);                           
            }
            // pid
            else{ 
                int pid = atoi(argv[1]);
                getjobpid(jobs,pid) -> state = FG; 
                kill(-pid,SIGCONT);  
                waitfg(pid);             
            }
        }
    }
    else{
        printf("Argument Required\n");
        return;
    }
    return;
}

/* 
 * waitfg - Block until process pid is no longer the foreground process
 */
void waitfg(pid_t pid)
{
    while(fgpid(jobs) == pid)
        sleep(0);

    return;
}

/*****************
 * Signal handlers
 *****************/

/* 
 * sigchld_handler - The kernel sends a SIGCHLD to the shell whenever
 *     a child job terminates (becomes a zombie), or stops because it
 *     received a SIGSTOP or SIGTSTP signal. The handler reaps all
 *     available zombie children, but doesn't wait for any other
 *     currently running children to terminate.  
 *     Some codes are borrowed from B&O pg. 727
 */
void sigchld_handler(int sig) 
{
    int status;
    pid_t pid;
    // Parent reaps children in no particular order
    while((pid = waitpid(-1, &status, WNOHANG|WUNTRACED)) > 0){
        //terminated normally
        if(WIFEXITED(status)){
            printf("child %d terminated normally with exit status=%d\n",pid,WEXITSTATUS(status));
            deletejob(jobs,pid);
        }
        //terminated by SIGINT
        else if(WIFSIGNALED(status)){
            printf("Job [%d] (%d) terminated by signal 2\n", pid2jid(jobs, pid), pid);
            deletejob(jobs,pid);
        }
        //stoped by SIGTSTP
        else if(WIFSTOPPED(status)){
            printf("Job [%d] (%d) terminated by signal 2\n", pid2jid(jobs, pid), pid);
            getjobpid(jobs,pid)->state = ST;
        }
    }

    return;
}

/* 
 * sigint_handler - The kernel sends a SIGINT to the shell whenver the
 *    user types ctrl-c at the keyboard.  Catch it and send it along
 *    to the foreground job.  
 */
void sigint_handler(int sig) 
{

    kill(-fgpid(jobs),SIGINT);
    return;
}

/*
 * sigtstp_handler - The kernel sends a SIGTSTP to the shell whenever
 *     the user types ctrl-z at the keyboard. Catch it and suspend the
 *     foreground job by sending it a SIGTSTP.  
 */
void sigtstp_handler(int sig) 
{
    kill(-fgpid(jobs), SIGTSTP);
    return;
}

/*********************
 * End signal handlers
 *********************/



/***********************
 * Other helper routines
 ***********************/

/*
 * usage - print a help message
 */
void usage(void) 
{
    printf("Usage: shell [-hvp]\n");
    printf("   -h   print this message\n");
    printf("   -v   print additional diagnostic information\n");
    printf("   -p   do not emit a command prompt\n");
    exit(1);
}

/*
 * sigquit_handler - The driver program can gracefully terminate the
 *    child shell by sending it a SIGQUIT signal.
 */
void sigquit_handler(int sig) 
{
    ssize_t bytes;
    const int STDOUT = 1;
    bytes = write(STDOUT, "Terminating after receipt of SIGQUIT signal\n", 45);
    if(bytes != 45)
       exit(-999);
    exit(1);
}



