/*
 * Driver code for airballoon problem
 */

//headers
#include <types.h>
#include <lib.h>
#include <thread.h>
#include <test.h>
#include <synch.h> //synchronization primitive

//Preprocessor defination
#define START 0
#define NROPES 16

//Global pointers and definations
static int ropes_left = NROPES;     //Global number of ropes 
struct lock *dec_ropes_left;        //Lock pointer for synchronizing decrements
struct lock *balloon_thread;        //Lock pointer for balloon 
struct lock *character_thread;      //lock pointers
struct cv *ropes_remaining;         //cv pointers
struct cv *thread_complete;         //cv pointers  

//Data structure for Mapping
typedef struct mapping {
int hook_value;     //hold hook for balloon
int stake_value;    //hold stake value 
int state;          //is the rope cut or not cut (-1 represents cut rope)
int rope_number;    //assign each rope a value for referencing
struct lock *lock;  //lock pointer for ropes
} MAPPING;


MAPPING ROPES[NROPES]; //create an array of 16 ropes



/*
 * Describe your design and any invariants or locking protocols 
 * that must be maintained. Explain the exit conditions. How
 * do all threads know when they are done?  
 */

/* Create ROPE data structures and create an array of size 16 representing
 * 16 ropes (each with its own lock pointers, state, hook and rope number) .
 * Using locking primitives (i.e. lock acquire, release), I was able to synchronize
 * between Marigold,Dandelion and Lord FlowerKiller. For each of the aforementioned threads,
 * they run until all ropes were severed and indicate that they are complete by sending cv_signal to balloon thread
 * (using condition variable). Once balloon thread receives the three cv_signal (the balloon thread was 3 wait statements)
 * it prints that it has completed. The main thread (airballoon) is waiting for cv signal
 * from balloon thread and after the balloon thread sends that (after printing that is is done and has freed the prince ), the main thread is done and the
 * program returns. We use state data member in the ROPE structure to represent cut/deleted rope (value=-1) and stack_value for updating 
 * the value for each rope when Lord Flowerkiller performs a change.
 *
 */

static
void
dandelion(void *p, unsigned long arg)
{
	(void)p;
	(void)arg;
	
	kprintf("Dandelion thread starting\n"); //indicate Danedelion is starting

    //loop until all ropes are cut
    while (ropes_left > 0){

    //random number generator
    int chosen = random() % NROPES;

    //lock acquire this rope before changing data values of ROPES data structure to ensure other threads cannot change values at the same time
    lock_acquire(ROPES[chosen].lock);

    //If the rope is not cut - proceed
    if (ROPES[chosen].state != -1){

        lock_acquire(dec_ropes_left);   //acquire lock for decrementing global number of rope left
        ROPES[chosen].state = -1;       //sever the rope
        kprintf("Dandelion severed rope %d\n",ROPES[chosen].rope_number);   //print statement
        ropes_left--;                   //decrement global number of ropes
        lock_release(dec_ropes_left);   //release lock for decrementing ropes
        }

    //release lock for selected rope
    lock_release(ROPES[chosen].lock);
    thread_yield();                     //yield thread
    }
    
    kprintf("Dandelion thread done\n");

    //send signal to balloon thread that Dandeloin has completed
    lock_acquire(character_thread);     //lock cv 
    cv_signal(thread_complete,character_thread); //signal balloon
    lock_release(character_thread);     //release cv
}




static
void
marigold(void *p, unsigned long arg)
{
	(void)p;
	(void)arg;
	
	kprintf("Marigold thread starting\n");      //print thread starting

    while (ropes_left > 0){                     //run until all ropes are cut 
    int chosen = random() % NROPES;             //generate randon number
	
    lock_acquire(ROPES[chosen].lock);           //lock selected rope
    
    
        if (ROPES[chosen].state != -1){         //if rope is not cut from before
        lock_acquire(dec_ropes_left);           //lock acquire global decrement value of ropes
        ROPES[chosen].state = -1;               //sever this rope
        
        kprintf("Marigold severed rope %d  from stake %d\n",ROPES[chosen].rope_number,ROPES[chosen].stake_value); //print statement
        
        ropes_left--;                           //decrement number of robes left
        lock_release(dec_ropes_left);           //release lock for global decrement
        }   

    lock_release(ROPES[chosen].lock);           //relaese lock for chosen rope
    
    thread_yield();                             //yield thread to allow other thread to run
    }

    kprintf("Marigold thread done\n");

    //send signal to balloon thread that Marigold has completed
    lock_acquire(character_thread);                                          
    cv_signal(thread_complete,character_thread);                             
    lock_release(character_thread);
}




static
void
flowerkiller(void *p, unsigned long arg)
{
	(void)p;
	(void)arg;
	
	kprintf("Lord FlowerKiller thread starting\n");     //print thread starting

    while (ropes_left > 0){                             //as long as not all ropes are severed
    int pick_rope = random() % NROPES;                  //pick random a rope
    int new_stake = random() % NROPES;                  //pick a random stake value to transfer that rope to

    lock_acquire(ROPES[pick_rope].lock);                //acquire lock for that rope

    //If the rope is not severed from before and current stake is not the same as the new random stake value
    //temporary variable store old stake value
    //And update stake_value field with new generated stake value
    //Print statement
        if (ROPES[pick_rope].state != -1 && ROPES[pick_rope].stake_value != new_stake){     
        int old_stake = ROPES[pick_rope].stake_value;
        ROPES[pick_rope].stake_value = new_stake;
        kprintf("Lord FlowerKiller switched rope %d from stake %d to stake %d\n",pick_rope,old_stake,new_stake);
        }
     
    //release lock of selected rope
    lock_release(ROPES[pick_rope].lock);
    thread_yield();
    }

    //print thread done
    kprintf("Lord Flowerkiller thread done\n");
   
    //send signal to balloon thread that FlowerKiller has completed
    lock_acquire(character_thread);                                          
    cv_signal(thread_complete,character_thread);                             
    lock_release(character_thread);
}

static
void
balloon(void *p, unsigned long arg)
{
	(void)p;
	(void)arg;
	
	kprintf("Balloon thread starting\n");       //Print start of thread
   
    //wait for the first three threads to finish and send their signal to balloon 
    lock_acquire(character_thread);
    cv_wait(thread_complete,character_thread);
    lock_release(character_thread);

    lock_acquire(character_thread);
    cv_wait(thread_complete,character_thread);
    lock_release(character_thread);

    lock_acquire(character_thread);
    cv_wait(thread_complete,character_thread);
    lock_release(character_thread);

    //Print freed statement
    kprintf("Balloon freed and Prince Dandelion escapes!\n");

    //Balloon thread is now as Marigold, FlowerKiller and Dandelion are complete
    kprintf("Balloon thread done\n");

     
    lock_acquire(balloon_thread);   //lock acquire
    cv_signal(ropes_remaining,balloon_thread);      //send signal to main thread that balloon thread is done
    lock_release(balloon_thread);   //release lock
}


// Change this function as necessary
int
airballoon(int nargs, char **args)
{
    int err = 0;

	(void)nargs;
	(void)args;
	(void)ropes_left;

    ropes_left = 16;    //initialize number of ropes

    //create locks
    balloon_thread = lock_create("balloon");
    character_thread = lock_create("characters");
    dec_ropes_left = lock_create("rope left");

    //create cv
    ropes_remaining = cv_create("cv_create");
    thread_complete = cv_create("first 3 threads complete");

    //assign values to each rope (stake,state, rope number, etc)
    for( int i = START; i < NROPES; i++ ){

    ROPES[i].lock = lock_create("create");
    ROPES[i].hook_value = i;
    ROPES[i].stake_value = i;
    ROPES[i].rope_number = i;
    ROPES[i].state = 0;
    }


    //generate child threads
	err = thread_fork("Marigold Thread",
			  NULL, marigold, NULL, 0);
	if(err)
		goto panic;
	
	err = thread_fork("Dandelion Thread",
			  NULL, dandelion, NULL, 0);
	if(err)
		goto panic;
	
	err = thread_fork("Lord FlowerKiller Thread",
			  NULL, flowerkiller, NULL, 0);
	if(err)
		goto panic;

	err = thread_fork("Air Balloon",
			  NULL, balloon, NULL, 0);
	if(err)
		goto panic;

	goto done;
panic:
	panic("airballoon: thread_fork failed: %s)\n",
	      strerror(err));
	
done:
    
    //wait for signal for balloon thread to finish and than print Main thread done
    lock_acquire(balloon_thread);
    cv_wait(ropes_remaining,balloon_thread);
    lock_release(balloon_thread);
	kprintf("Main thread done\n");


    //destroy locks and cv created
    for( int i = START; i < NROPES; i++ ){
    lock_destroy(ROPES[i].lock);
    }
    
    lock_destroy(balloon_thread);
    lock_destroy(character_thread);
    lock_destroy(dec_ropes_left);
    cv_destroy(thread_complete);
    cv_destroy(ropes_remaining);

    return 0;
}
