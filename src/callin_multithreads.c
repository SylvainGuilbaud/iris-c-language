#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include "iris-callin.h"
#include <signal.h>

// https://docs.intersystems.com/irislatest/csp/docbook/Doc.View.cls?KEY=BXCI_callin#BXCI_callin_multithreading_signals

/*
SIGINTのハンドラが、IRISが起動していなくてIRISSTARTが失敗した時に有効化されない模様。Ctrl-cでハンドラが呼ばれることなくイメージが終了してしまう。
IRISSTART()成功時には有効→Ctrl-cでsigaction_handler_asyncが動作する
IRISSTART()を全く呼ばない場合も有効→Ctrl-cでsigaction_handler_asyncが動作する
IRISSTART()成功後にIRISEND()を実行した場合も(//runtest(*p);)有効→Ctrl-cでsigaction_handler_asyncが動作する

*/
#define usesigaction 1

#ifdef usesigaction
void sigaction_handler();
#else
void signal_handler();
#endif

int	runtest();
void *thread_main(void *);          // threads which connect to IRIS
#if 1
void *thread_noiris_main(void *);   // a thread for non IRIS
void sigaction_handler_async();     // signal handler for Asynchronous signals
volatile sig_atomic_t eflag = 0;    // flag to end thread_noiris_main
#endif 

#define NUMTHREADS 3	/* Number of threads */

/*
    This is a sample of how to utilize the call-in interface in a multi-threaded environment.
        It starts multiple threads, and within each thread calls IRISStart to create a connection.
        The thread then performs one action via IrisExecute().  The thread then calls IRISEnd()
        and exits.
    When all the threads have exited, the main thread exits.

*/
int main(int argc, char* argv[])
{
  int	threadcnt = 0;
  pthread_t th[NUMTHREADS];
  int targ[NUMTHREADS];
  pthread_t th2;

  int rtn;
  int	rc;
  int	numthreads = NUMTHREADS;
  int	i;
  
  printf("Starting main process. #%ld\n",pthread_self());

  // signal handler for Synchronous signals
#ifdef usesigaction
  struct sigaction sa;
  memset(&sa, 0, sizeof(struct sigaction));
  sigemptyset(&sa.sa_mask);
  sa.sa_sigaction = sigaction_handler;
  sa.sa_flags   = SA_SIGINFO;
  sigaction(SIGSEGV, &sa, NULL);
#else
  if ( signal(SIGSEGV, signal_handler) == SIG_ERR ) {
    pthread_exit(0);
  } 
#endif

  // signal handler for Asynchronous signals, such as SIGINT
#if 1
  struct sigaction sa_asyncsig;
  memset(&sa_asyncsig, 0, sizeof(sa_asyncsig));
  sa_asyncsig.sa_sigaction = sigaction_handler_async;
  sa_asyncsig.sa_flags = SA_SIGINFO;

  if ( sigaction(SIGINT, &sa_asyncsig, NULL) < 0 ) {
    exit(1);
  }
#endif

  // creating a non IRIS thread
  rtn = pthread_create(&th2, NULL, thread_noiris_main, (void *) NULL);

  rc=IRISSETDIR("/usr/irissys/mgr");
  printf("IRISSETDIR rc:%d\n",rc);

  for (i=0; i < numthreads; i++) {
    targ[i]=i;
    rtn = pthread_create(&th[i], NULL, thread_main, (void *) &targ[i]);
    if (rtn != 0) {
        fprintf(stderr, "pthread_create() #%0d failed for %d.", i, rtn);
        exit(EXIT_FAILURE);
    }
  }
  printf("Waiting for threads to exit...\n");
 
  for (i=0; i < numthreads; i++) {
    pthread_join(th[i], NULL);
  }
#if 1
  printf("Join th2\n");
  pthread_join(th2, NULL);
#endif
  // Wait for the threads to exit
  printf("All threads have exited - done\n");
  return 0;
}

void *thread_main(void *tparam) {
  int *p = (int *)tparam;
  int termflag = IRIS_TTNONE|IRIS_TTNEVER;
  int timeout = 15;
  IRISSTR pin;
  IRISSTR pout;
  IRISSTR pusername;
  IRISSTR ppassword;
  IRISSTR pexename;
  int	rc;
  int retval=1;
  
  strcpy((char *) pexename.str,"samplecallint");
  pexename.len = strlen((char *) pexename.str);

  /*
  strcpy((char *) pin.str,"/dev/null");
  strcpy((char *) pout.str,"/dev/null");
  pin.len = strlen((char *) pin.str);
  pout.len = strlen((char *)pout.str);
  */

  strcpy((char *) pusername.str,"_SYSTEM");
  strcpy((char *) ppassword.str,"SYS");
  pusername.len = strlen((char *) pusername.str);
  ppassword.len = strlen((char *)ppassword.str);

  printf("Thread(%d) #%ld starting authentication in IRIS'\n",*p,pthread_self());

  /* Authenticate using username/pw user. */
  rc = IRISSECURESTART(&pusername,&ppassword,&pexename,termflag, timeout, NULL, NULL);
  if (rc != IRIS_SUCCESS) {
    IRISEND();
    if (rc == IRIS_ACCESSDENIED) {
      printf("Thread(%d) #%ld : IRISSecureStart returned Access Denied\n",*p,pthread_self());
    }
    else if (rc == IRIS_CHANGEPASSWORD) {
      printf("Thread(%d) #%ld : IRISsecureStart returned Password Change Required\n",*p,pthread_self());
    }
    else {
      printf("Thread(%d) #%ld : IRISSecureStart returned %d\n",*p,pthread_self(), rc);
    }
    return NULL;
  }

  runtest(*p);

  printf("Thread(%d) #%ld leaving IRIS'\n",*p,pthread_self());
  IRISEND();

  printf("Thread(%d) #%ld exiting\n",*p,pthread_self());

  return NULL;
}

int runtest(int p) {
  int rc;
  Callin_char_t *gloref="callinMT";
  unsigned int newId;
  char date[64];
  time_t t;
  struct tm local;
  int *foo = NULL;

  printf("Thread(%d) #%ld starting test\n",p,pthread_self());

  // Uncomment a below line to tigger SIGSEGV intentionally.
  if (p==0) { printf("Thread(%d) #%ld is firing SIGSEGV.\n",p,pthread_self()); *foo = 1; } 

  while ( !eflag ) {

    // Do some dummy work
    sleep(rand()%5+5);

    t=time(NULL);
    localtime_r(&t,&local);
    strftime(date, sizeof(date), "%Y/%m/%d %a %H:%M:%S", &local);

    // Get new sequence value.  Equivalent of Set newId=$INCREMENT(^callinMT)
    rc = IRISPUSHGLOBAL(strlen((const char *)gloref), gloref);
    rc = IRISPUSHINT(1); // Increment by 1
    rc = IRISGLOBALINCREMENT(0);
    rc = IRISPOPINT(&newId);

    // Set ^callinMT(newId)=timestamp
    int subsc=0;
    rc = IRISPUSHGLOBAL(strlen((const char *)gloref), gloref);
    rc = IRISPUSHINT(newId); subsc++;     /* subscript */
    rc = IRISPUSHSTR(strlen(date),date);  /* value */
    rc = IRISGLOBALSET(subsc);  
    if (rc!=IRIS_SUCCESS) { 
      return -1;
    }
  }
  printf("Thread(%d) #%ld has completed test\n",p,pthread_self());
  return 0;
}

#ifdef usesigaction
void sigaction_handler(int signal, siginfo_t *si, void *arg)
{
    printf("Caught signal #%ld\n",pthread_self());
    // Somehow si is not sset... ?
    if (si!=NULL) printf("Caught signal(%d) via sigaction_handler() Thread #%ld\n", si->si_signo,pthread_self());
    //IRISEND();  // Do not call IRISEND() here. It causes another SIGSEGV  
    pthread_exit(0);
}
#else
void signal_handler(int sig) {
    printf("Caught segfault via signal() Thread #%ld\n",pthread_self());
    //IRISEND();  // Do not call IRISEND() here. It causes another SIGSEGV  
    pthread_exit(0);
}
#endif
#if 1
void sigaction_handler_async(int sig, siginfo_t *info, void *ctx) {
  printf("Signal caught by #%ld\n",pthread_self());
  if (info!=NULL) {
    printf("si_signo:%d\nsi_code:%d\n", info->si_signo, info->si_code);
    printf("si_pid:%d\nsi_uid:%d\n", (int)info->si_pid, (int)info->si_uid);
  }
  eflag = 1;
  // Do not exit here because doing so will leave child processes (IRIS processes) as zombie...
  // Instead, set eflag to finish threads which is connected to IRIS via IRISSTART(). 
}
#endif

void *thread_noiris_main(void *tparam) {
  printf("Starting %s #%ld\n",__func__,pthread_self());
  while ( !eflag ) { sleep(1); }
  printf("Ending %s #%ld\n", __func__,pthread_self());
  return 0;
}

