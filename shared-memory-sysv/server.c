#include <linux/module.h>   // init_module, cleanup_module //
#include <linux/kernel.h>   // KERN_INFO //
#include <linux/types.h>    // uint64_t //
#include <linux/kthread.h>  // kthread_run, kthread_stop //
#include <linux/delay.h>    // msleep_interruptible //

#include <linux/syscalls.h> // sys_shmget //

#include "shm_bmk.h"


// External declarations //
extern long k_shmat( int shmid );
extern long k_semop( int semid, struct sembuf *tsops,
                        unsigned int nsops );


// Function prototypes //
static void handle_message( void );
static int message_ready( void );
static int run_thread( void *data );
static void send_kernel_timing( uint64_t cycles );

// Global variables //
static struct task_struct *shm_task = NULL;
static void *shm                    = NULL;
static int shmid;
static int semid;


/**
 * Read the pentium time stamp counter register.
 * 
 * @return The number of cycles that have elapsed since boot.
 */
__inline__ uint64_t bmk_rdtsc( void )
{
    uint64_t x;
    __asm__ volatile("rdtsc\n\t" : "=A" (x));
    return x;
}


/**
* Called each time a client wishes to benchmark.
*/
static void handle_message( void )
{
    int i;
    char msg[BUFSIZ];
    uint64_t kernel_cycles;
    uint64_t start;
    uint64_t stop;
    uint64_t difference;
    struct sembuf sb = {0, 0, 0};

    kernel_cycles = 0;
    sb.sem_op = -1; // Lock sem 0 //
    if( k_semop( semid, &sb, 1 ) == -1 )
    {
        printk( KERN_INFO "SERVER : Unable to lock sem 0\n" );
        return;
    }

    for( i = 0; i < TRIALS; i++ )
    {
        strncpy( msg, "~Thanks for the message Client", BUFSIZE );
        //printk( KERN_INFO "SERVER : Sending message: %s\n",
        //
        msg );
        start = bmk_rdtsc();
        memcpy( shm, msg, BUFSIZE );
        stop = bmk_rdtsc();
        difference = stop - start;
        //printk( KERN_INFO "SERVER : Number of cycles: %llu\n",
        //difference );
        kernel_cycles = kernel_cycles + difference;
    }

    printk( KERN_INFO "SERVER : Total cycles: %llu\n",
            kernel_cycles );
    send_kernel_timing( kernel_cycles );
    sb.sem_op = 1; // Free sem 0 //
    if( k_semop( semid, &sb, 1 ) == -1 )
    {
        printk( KERN_INFO "SERVER : Unable to free sem 0\n" );
        return;
    }

}

/**
* Checks the shared memory for messages (peeks, but does not remove).
*
* @return TRUE (1) if message is ready, FALSE (0) otherwise.
*/
static int message_ready( void )
{
    if(strncmp( shm, "*", sizeof( char ) ) == 0 )
    {
        return TRUE;
    }
    return FALSE;
}

/**
* The entry point of the kernel thread which is the message benchmark
* server.
*
* @param data Any parameters for the kernel thread.
* @return  The kernel thread exit status.
*/
static int run_thread(void *data)
{
    union semun arg;

    semid = sys_semget( KEY, 1 066 | IPC_CREAT );

    if( semid == -1 )
    {
        printk( KERN_INFO "SERVER : Unable to obtain semid\n" );
        return -1;
    }
    // Note: sys_semctl only handles SETVAL and RMID from kernel
    // space currently

    arg.val = 1;
    if( sys_semctl( semid, 0, SETVAL, arg ) == -1 )
    {
        printk( KERN_INFO
        "SERVER : Unable to initialize sem 0\n" );
        return -1;
    }
    shmid = sys_shmget( KEY, BUFSIZE, 0666 | IPC_CREAT );

    if( shmid < 0 )
    {
        printk( KERN_INFO "SERVER : Unable to obtain shmid\n" );
        return -1;
    }
    shm = (void *)k_shmat( shmid );
    printk( KERN_INFO "SERVER : Address is %p\n", shm );

    if( !shm )
    {
        printk( KERN_INFO
        "SERVER : Unable to attach to memory\n" );
        return -1;
    }
    strncpy( shm, "~", sizeof( char ) );

    while( !kthread_should_stop() )
    {
        if( message_ready() )
        {
            printk( KERN_INFO "SERVER : Message ready\n" );
            handle_message();
        }
        msleep_interruptible( 1000 );
    }
    return 0;
}

/**
* Pass raw integer timing results to user space where
* floating point operations are allowed.
*
* @param cycles The raw cycles.
*/
static void send_kernel_timing( uint64_t cycles )
{
    memcpy( shm + 1, &cycles, sizeof( uint64_t ) );
}


/**
* Entry point of module execution.
*
* @return The status of the module initialization.
*/
int init_module()
{
    printk( KERN_INFO "SERVER : Initializing shm_server\n" );
    shm_task = kthread_run( run_thread, NULL, "shm_server" );
    return 0;
}

/**
* Exit point of module execution.
*/
void cleanup_module()
{
    int result;
    union semun arg;
    printk( KERN_INFO "SERVER : Cleaning up shm_server\n" );
    result = kthread_stop( shm_task );
    if( result < 0 )
    {
        printk( KERN_INFO "SERVER : Unable to stop shm_task\n" );
    }

    result = sys_shmctl( shmid, IPC_RMID, NULL );
    if( result < 0 )
    {
        printk( KERN_INFO
        "SERVER : Unable to remove shared memory from system\n" );
    }
    result = sys_semctl( semid, 0, IPC_RMID, arg );
    if( result == -1 )
    {
        printk( KERN_INFO
        "SERVER : Unable to remove semaphore\n" );
    }
}

MODULE_LICENSE( "GPL" );
MODULE_AUTHOR( "Ryan Slominski" );
MODULE_DESCRIPTION( "Shared memory benchmark server" );