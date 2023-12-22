/*
 * tsh - A tiny shell program with job control
 */

/************** 
*Dillon Gaughan
**************/


/****** I found a functional layout from skywalker212 and proceeded to optimize and
remove redundancies I found. I broke up many of the functions into smaller helpers
and changed some of the logic. I did this because I believe it makes it much easier to
understand what is happening at each step of the program. I am not as concerned with speed
here so the presence of more function calls does not seem to be as much of an issue as the 
previous lab. I also instituted a broader error handling function to cover most situations.
-******/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>

/* constants */
#define MAXLINE    1024   /* max line size */
#define MAXARGS     128   /* max args on a command line */
#define MAXJOBS      16   /* max jobs at any point in time */
#define MAXJID    1<<16   /* max job ID */

/* Job states */
#define UNDEF 0 /* undefined */
#define FG 1    /* running in foreground */
#define BG 2    /* running in background */
#define ST 3    /* stopped */

/*
 * Jobs states: FG (foreground), BG (background), ST (stopped)
 * Job state transitions and enabling actions:
 *     FG -> ST  : ctrl-z
 *     ST -> FG  : fg command
 *     ST -> BG  : bg command
 *     BG -> FG  : fg command
 * At most 1 job can be in the FG state.
 */

/* Global variables */
extern char **environ;      /* defined in libc */
char prompt[] = "tsh> ";    /* command line prompt (DO NOT CHANGE) */
int verbose = 0;            /* if true, print additional output */
pid_t mainpid;              /* to store the process id of the main function */
int nextjid = 1;            /* next job ID to allocate */
char sbuf[MAXLINE];         /* for composing sprintf messages */

struct job_t {              /* The job struct */
    pid_t pid;              /* job PID */
    int jid;                /* job ID [1, 2, ...] */
    int state;              /* UNDEF, BG, FG, or ST */
    char cmdline[MAXLINE];  /* command line */
};
struct job_t jobs[MAXJOBS]; /* The job list */
typedef struct job_t job_t; /* We don't want to write struct job_t everytime */

/***** error handler *****/
typedef enum {
    ERR_NO_ARG,
    ERR_NO_SUCH_JOB,
    ERR_NO_SUCH_PROCESS, 
    ERR_INVALID_ID, 
    ERR_NO_CMD,
    ERR_APP,
    ERR_UNIX
} ErrorType;

/***** Function Headers *****/

void eval(char *cmdline);
int builtin_cmd(char **argv);
void do_bgfg(char **argv);
void waitfg(pid_t pid);

void sigchld_handler(int sig);
void sigtstp_handler(int sig);
void sigint_handler(int sig);

int valid_argument(char*);                 
void arg_list(char* buf, char** argv, int* argc);
char* process_argument(char* buf, char** argv, int* argc);
char* arg_delim(char* buf);
char* skip_spaces(char* buf);
int parseline(const char *cmdline, char **argv);
void sigquit_handler(int sig);
void execute_command(char *argv[], sigset_t *mask, char *cmdline, int bg);
void setup_signal_handlers(sigset_t *mask);

void clearjob(struct job_t *job);
void initjobs(struct job_t *jobs);
int maxjid(struct job_t *jobs);
int addjob(struct job_t *jobs, pid_t pid, int state, char *cmdline);
int deletejob(struct job_t *jobs, pid_t pid);
pid_t fgpid(struct job_t *jobs);
struct job_t *getjobpid(struct job_t *jobs, pid_t pid);
struct job_t *getjobjid(struct job_t *jobs, int jid);
int pid2jid(pid_t pid);
void listjobs(struct job_t *jobs);

void usage(void);
void handle_error(ErrorType err, const char *info);
typedef void handler_t(int);
handler_t *Signal(int signum, handler_t *handler);


/****************** 
*Main program start
******************/


int main(int argc, char **argv)
{
    char c;
    char cmdline[MAXLINE];
    mainpid = getpid();
    int emit_prompt = 1; 
    dup2(1, 2);
    while ((c = getopt(argc, argv, "hvp")) != EOF) {
        switch (c) {
        case 'h':             
            usage();
	    break;
        case 'v':             
            verbose = 1;
	    break;
        case 'p':             
            emit_prompt = 0; 
	    break;
	default:
            usage();
	}
    }
    Signal(SIGINT,  sigint_handler);   
    Signal(SIGTSTP, sigtstp_handler);  
    Signal(SIGCHLD, sigchld_handler);  
    Signal(SIGQUIT, sigquit_handler);
    initjobs(jobs);
    while (1) {
	if (emit_prompt) {
	    printf("%s", prompt);
	    fflush(stdout);
	}
	if ((fgets(cmdline, MAXLINE, stdin) == NULL) && ferror(stdin))
	    handle_error(ERR_APP, "fgets error");
	if (feof(stdin)) { 
	    fflush(stdout);
	    exit(0);
	}
	eval(cmdline);
	fflush(stdout);
	fflush(stdout);
    }
    exit(0); 
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
 * when we type ctrl-c (ctrl-z) at the keyboard.*/

void eval(char *cmdline) {
    char *argv[MAXARGS];
    int bg; 
    sigset_t mask;

    bg = parseline(cmdline, argv);
    if (argv[0] == NULL) 
        return;

    if (!builtin_cmd(argv)) {  
        setup_signal_handlers(&mask);  
        execute_command(argv, &mask, cmdline, bg);  
    }
}

/************************* 
*Helper functions for eval
*************************/

void setup_signal_handlers(sigset_t *mask) {
    if (sigemptyset(mask) < 0)
        handle_error(ERR_UNIX, "sigemptyset error");
    if (sigaddset(mask, SIGCHLD))
        handle_error(ERR_UNIX, "sigaddset error");
    if (sigprocmask(SIG_BLOCK, mask, NULL) < 0)
        handle_error(ERR_UNIX, "sigprocmask error");
}

/***** Executing commands that are not built in *****/

void execute_command(char *argv[], sigset_t *mask, char *cmdline, int bg) {
    pid_t pid;
    if ((pid = fork()) < 0) {
        handle_error(ERR_UNIX, "Forking Error!");
        return;
    } else if (pid == 0) {  // Child process
        setpgid(0, 0);
        sigprocmask(SIG_UNBLOCK, mask, NULL);
        if (execvp(argv[0], argv) < 0) {
            handle_error(ERR_NO_CMD, argv[0]);
            exit(1);
        }
    } else { //Parent process
        if (!bg) 
            addjob(jobs, pid, FG, cmdline);
        else 
            addjob(jobs, pid, BG, cmdline);

        sigprocmask(SIG_UNBLOCK, mask, NULL);
        if (!bg) 
            waitfg(pid);
        else 
            printf("[%d] (%d) %s", pid2jid(pid), pid, cmdline);
    }
}

/***** parse the command line *****/

int parseline(const char* cmdline, char** argv) {
    static char array[MAXLINE];  
    char* buf = array;          
    int argc = 0;                
    int bg;                     

    strcpy(buf, cmdline);
    buf[strlen(buf) - 1] = ' ';  
    buf = skip_spaces(buf);
    arg_list(buf, argv, &argc);
    if (argc == 0) {  
        return 1;
    }
    bg = (argv[argc - 1][0] == '&');
    if (bg) {
        argv[--argc] = NULL; 
    }
    return bg; 
}

/****************************** 
*Helper functions for parseline
******************************/

/***** skip leading spaces *****/
char* skip_spaces(char* buf) {
    while (*buf && (*buf == ' ')) {
        buf++;
    }
    return buf;
}

/***** Find next argument delimiter *****/

char* arg_delim(char* buf) {
    char* delim;
    if (*buf == '\'') {
        buf++;
        delim = strchr(buf, '\'');
    } else {
        delim = strchr(buf, ' ');
    }
    return delim;
}

/***** Process a single argument *****/

char* process_argument(char* buf, char** argv, int* argc) {
    char* delim = arg_delim(buf);

    if (delim) {
        argv[(*argc)++] = buf;
        *delim = '\0';
        buf = delim + 1;
        buf = skip_spaces(buf);
    }

    return buf;
}

/***** Build argv list from cmd line *****/

void arg_list(char* buf, char** argv, int* argc) {
    while ((buf = process_argument(buf, argv, argc)) && *buf != '\0') {
    }
    argv[*argc] = NULL;
}


/***** builtin_cmd - If the user has typed a built-in command
 then execute it immediately. *****/

int builtin_cmd(char **argv)
{
    if(strcmp(argv[0],"quit")==0){              
        int i;
        for(i=0;i<MAXJOBS;i++){
            if(jobs[i].state == BG) {
                waitfg(jobs[i].pid);
            }
        }
        exit(0);
	}else if(strcmp(argv[0],"jobs")==0){     
		listjobs(jobs);
		return 1;
    }else if(strcmp(argv[0],"bg")==0 || strcmp(argv[0],"fg")==0){   
		do_bgfg(argv);
		return 1;
	}else return 0;                             
}


/***** do_bgfg - Execute the builtin bg and fg commands. *****/

void do_bgfg(char **argv) {
    if (!argv[1]) {
        handle_error(ERR_NO_ARG, argv[0]);
        return;
    }
    char *id = argv[1];
    int is_jid = id[0] == '%';
    if (!valid_argument(id)) {
        handle_error(ERR_INVALID_ID, argv[0]);
        return;
    }
    // Determine if we're working with a PID or JID
    int job_number = atoi(id + (is_jid ? 1 : 0));  // Skip '%' for JID
    job_t* job = is_jid ? getjobjid(jobs, job_number) : getjobpid(jobs, job_number);
    if (!job) {
        if (is_jid) {
            handle_error(ERR_NO_SUCH_JOB, argv[1]);
        } else {
            handle_error(ERR_NO_SUCH_PROCESS, argv[1]);
        }
        return;
    }
    //Continue the job
    if (kill(-(job->pid), SIGCONT) < 0) {
        perror("kill (SIGCONT)");
        return;
    }
    // Update job state and wait if necessary
    job->state = strcmp(argv[0], "fg") == 0 ? FG : BG;
    if (job->state == FG) {
        waitfg(job->pid);
    } else {
        printf("[%d] (%d) %s", job->jid, job->pid, job->cmdline);
    }
}

/***** determine if the argument is a number *****/

int valid_argument(char *tmp){         
    int j,isDigit;
    if(tmp[0]=='%') j = 1;
    else j = 0;
    while(j<strlen(tmp)){
      isDigit = isdigit(tmp[j]);
      if (isDigit == 0) break;
      j++;
    }
    if(j==strlen(tmp)) return 1;
    else return 0;
}


/***** waitfg - Block until process pid is no longer the foreground
 process *****/

void waitfg(pid_t pid)          
{
    job_t* temp = getjobpid(jobs,pid);
    while(temp->state == FG){
        sleep(1);
    }
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
 */
void sigchld_handler(int sig)
{
    pid_t pid;
    int status;
    while (1) { 
       pid = waitpid(-1, &status, WNOHANG | WUNTRACED);     //non blocking wait
       job_t* temp = getjobpid(jobs,pid); 
       if (pid <= 0)  break;                                // No more zombie children to reap.
       else{
        if(WIFEXITED(status)){                          //if child exited normally then delete the job
            temp->state = UNDEF;
            deletejob(jobs,pid);
        }else if(WIFSIGNALED(status)){                       //if child was sent a termination signal then display the message and delete the job
            printf("Job [%d] (%d) terminated by signal %d\n",pid2jid(pid),pid,WTERMSIG(status));
            if(temp->state == FG) deletejob(jobs,pid);
        }else if(WIFSTOPPED(status)){                           //if child was stopped then change it's status to stopped
            temp->state = ST;
            printf("Job [%d] (%d) stopped by signal %d\n",pid2jid(pid),pid,WSTOPSIG(status));
        }
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
    int pid = fgpid(jobs);
    if(pid!=0) kill(-pid,SIGINT);                  //send sigint to the process group
    return;
}

/*
 * sigtstp_handler - The kernel sends a SIGTSTP to the shell whenever
 *     the user types ctrl-z at the keyboard. Catch it and suspend the
 *     foreground job by sending it a SIGTSTP.
 */
void sigtstp_handler(int sig)
{
    int pid = fgpid(jobs);
    if(pid!=0) kill(-pid,SIGTSTP);                  //send sigtstp to the process group
    return;
}

/*********************
 * End signal handlers
 *********************/

/***********************************************
 * Helper routines that manipulate the job list
 **********************************************/

/* clearjob - Clear the entries in a job struct */
void clearjob(struct job_t *job) {
    job->pid = 0;
    job->jid = 0;
    job->state = UNDEF;
    job->cmdline[0] = '\0';
}

/* initjobs - Initialize the job list */
void initjobs(struct job_t *jobs) {
    int i;
    for (i = 0; i < MAXJOBS; i++)
	clearjob(&jobs[i]);
}

/* maxjid - Returns largest allocated job ID */
int maxjid(struct job_t *jobs)
{
    int i, max=0;

    for (i = 0; i < MAXJOBS; i++)
	if (jobs[i].jid > max)
	    max = jobs[i].jid;
    return max;
}

/* addjob - Add a job to the job list */
int addjob(struct job_t *jobs, pid_t pid, int state, char *cmdline)
{
    int i;
    if (pid < 1)
	return 0;
    for (i = 0; i < MAXJOBS; i++) {
	    if (jobs[i].pid == 0) {
	        jobs[i].pid = pid;
	        jobs[i].state = state;
	        jobs[i].jid = nextjid++;
	        if (nextjid > MAXJOBS)
		    nextjid = 1;
	        strcpy(jobs[i].cmdline, cmdline);
  	        if(verbose){
	            printf("Added job [%d] %d %s\n", jobs[i].jid, jobs[i].pid, jobs[i].cmdline);
            }
            return 1;
	    }
    }
    printf("Tried to create too many jobs\n");
    return 0;
}

/* deletejob - Delete a job whose PID=pid from the job list */
int deletejob(struct job_t *jobs, pid_t pid)
{
    int i;
    if (pid < 1)
	return 0;
    for (i = 0; i < MAXJOBS; i++) {
	    if (jobs[i].pid == pid) {
	        clearjob(&jobs[i]);
	        nextjid = maxjid(jobs)+1;
	        return 1;
	    }
    }
    return 0;
}

/* fgpid - Return PID of current foreground job, 0 if no such job */
pid_t fgpid(struct job_t *jobs) {
    int i;
    for (i = 0; i < MAXJOBS; i++)
	if (jobs[i].state == FG)
	    return jobs[i].pid;
    return 0;
}

/* getjobpid  - Find a job (by PID) on the job list */
struct job_t *getjobpid(struct job_t *jobs, pid_t pid) {
    int i;
    if (pid < 1)
	return NULL;
    for (i = 0; i < MAXJOBS; i++)
	if (jobs[i].pid == pid)
	    return &jobs[i];
    return NULL;
}

/* getjobjid  - Find a job (by JID) on the job list */
struct job_t *getjobjid(struct job_t *jobs, int jid)
{
    int i;
    if (jid < 1)
	return NULL;
    for (i = 0; i < MAXJOBS; i++)
	if (jobs[i].jid == jid)
	    return &jobs[i];
    return NULL;
}

/* pid2jid - Map process ID to job ID */
int pid2jid(pid_t pid)
{
    int i;
    if (pid < 1)
	return 0;
    for (i = 0; i < MAXJOBS; i++)
	if (jobs[i].pid == pid) {
            return jobs[i].jid;
        }
    return 0;
}

/* listjobs - Print the job list */
void listjobs(struct job_t *jobs)
{
    int i;
    for (i = 0; i < MAXJOBS; i++) {
	    if (jobs[i].pid != 0) {
	        printf("[%d] (%d) ", jobs[i].jid, jobs[i].pid);
	        switch (jobs[i].state) {
		    case BG:
		        printf("Running ");
		        break;
		    case FG:
		        printf("Foreground ");
		        break;
		    case ST:
		        printf("Stopped ");
		        break;
	        default:
		        printf("listjobs: Internal error: job[%d].state=%d ",
			    i, jobs[i].state);
	        }
	        printf("%s", jobs[i].cmdline);
	    }
    }
}
/******************************
 * end job list helper routines
 ******************************/


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

/***** Switch Case function to handle most error situations. *****/

void handle_error(ErrorType err, const char *msg) {
    switch (err) {
        case ERR_NO_ARG:
            fprintf(stderr,"%s command requires PID or %%jobid argument\n", msg);
            break;
        case ERR_NO_SUCH_JOB:
            fprintf(stderr, "%s: No such job\n", msg);
            break;
        case ERR_NO_SUCH_PROCESS:
            fprintf(stderr, "(%s): No such process\n", msg);
            break;            
        case ERR_INVALID_ID:
            fprintf(stderr, "%s: argument must be a PID or %%jobid\n", msg);
            break;
        case ERR_NO_CMD:
            fprintf(stderr, "%s: Command not found\n", msg);
            break;
        case ERR_UNIX:
            fprintf(stderr, "%s: %s\n", msg, strerror(errno)); // Print the system error message
            exit(1);
        case ERR_APP:
            fprintf(stderr, "%s\n", msg); // Print the application-specific error message
            exit(1);
        default:
            fprintf(stderr, "An unknown error occurred: %s\n", msg);
            break;
    }
}    
/*
 * Signal - wrapper for the sigaction function
 */
handler_t *Signal(int signum, handler_t *handler)
{
    struct sigaction action, old_action;

    action.sa_handler = handler;
    sigemptyset(&action.sa_mask); /* block sigs of type being handled */
    action.sa_flags = SA_RESTART; /* restart syscalls if possible */

    if (sigaction(signum, &action, &old_action) < 0)
	handle_error(ERR_UNIX, "Signal error");
    return (old_action.sa_handler);
}

/*
 * sigquit_handler - The driver program can gracefully terminate the
 *    child shell by sending it a SIGQUIT signal.
 */
void sigquit_handler(int sig)
{
    printf("Terminating after receipt of SIGQUIT signal\n");
    exit(1);
}
