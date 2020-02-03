#include "EnvController.h"

#ifndef OS_2016_EX3_MACRO_DUMMY

#include <iostream>
#include <map>
#include <unistd.h>
#include <vector>
#include <sys/time.h>
#include <sys/types.h>
#include <fstream>
#include <sstream>
#include "DebugClient.h"
#include "MapReduceFramework.h"
#include <pthread.h>
#include <bits/unique_ptr.h>
#include <memory>

//
//while(++os < linux);
//
//os *= os;
//
//return os;
// We're onto something... ^_^
using namespace std;
#define  CALLING_THREAD_ID -1
#define NUM_THREADS 5
#define MAP_CHUNK_SIZE 2
#define EXEC_MAP_STARTING_INDEX 1
#define SUCCESS 0
#define REDUCE_CHUNK_SIZE 1
#define WAIT_TIME_SECONDS       1
#define MICRO_SEC_TO_NANO_SEC 1000
#define WAIT_TIME_INTERVAL 10000000
#define BUF_SIZE 30

#define FAIL_VAL -1
#define MILISEC_FACTOR 1000000
#define MILI_TO_NANO 1000

///---------------typedefs------------------------------------------
typedef std::pair<k2Base *, v2Base *> K2_V2_ITEM;
typedef std::pair<k3Base *, v3Base *> K3_V3_ITEM;
typedef _List_iterator<IN_ITEM> K1_LIST_ITER;

//A comparison operator for the multimap.
struct ltk3base {
    bool operator()(const k3Base *s1, const k3Base *s2) const
    {
        return (*s1) < (*s2);
    }
};

//A comparison operator for the multimap.
struct ltk2base {
    bool operator()(const k2Base *s1, const k2Base *s2) const
    {
        return (*s1) < (*s2);
    }
};

struct char_cmp_base {
    bool operator()(const char s1, const char s2) const
    {
        return (s1) < (s2);
    }
};

//typedef std::pair<k2Base*, v2Base*> MID_ITEM;
//typedef std::multimap<k2Base*, v2Base*, ltk2base> MID_ITEMS_MULTIMAP;
typedef std::map<k2Base *, V2_LIST *, ltk2base> MID_ITEMS_MAP;
//this is our "homebrew" multimap
//typedef std::map<k2Base*, list<v2Base*>*, ltk2base> MID_ITEMS_MAP_OF_LISTS;
typedef std::multimap<k3Base *, v3Base *, ltk3base> FINAL_ITEMS_MAP;
///----------------------------------------------------------------

//for debug:
//std::multimap<char, int, char_cmp_base>  temp_map;
MID_ITEMS_MAP shuffle_output_map;

//the map in which shuffle throws all it's [...]
//MID_ITEMS_MAP_OF_LISTS shuffle_output_map_of_lists;



FINAL_ITEMS_MAP reduce_output_multimap;

//array of pthreads
vector<pthread_t> threads;
int real_thread_num = 0;

//index that counts how many elements were read from the global input list.
unsigned long shared_index = 0;

//the global k1v1 list holding the input data.
IN_ITEMS_LIST g_input_list;
unsigned long g_list_length;
//The global iterator for the k1v1 list. it's access is shared between the
//execmaps
K1_LIST_ITER g_input_list_shared_iterator;

//For reduce -> This is the iterator shared between exec-reduce functions.
MID_ITEMS_MAP::iterator g_mid_items_map_shared_iterator;
unsigned long g_mid_items_map_lenght;

//a MAP of <thread_id, thread_index>. the id is the os' perspective the index
// is our programs perspective
std::map<unsigned long, int> thread_id_index_map;
//------------------------------------------------------------------------------

//The emit2 funtion emits into this temporary data structure
//vector<list<K2_V2_ITEM>*> emit2_k2v2_output_container;
vector<std::shared_ptr<list<K2_V2_ITEM>>> emit2_k2v2_output_container;

//The emit3 funtion emits into this temporary data structure.
//(for each thread).
//vector<list<OUT_ITEM>*> g_reduce_output_container;
vector<std::shared_ptr<list<OUT_ITEM>>> g_reduce_output_container;

bool IS_COMPARE = false; //For debugging.

void *status; //Needed for joining threads.

///MUTEXES//---------------------------------------------------

//locks the data dump shared between each exec-map and the shuffle
vector<pthread_mutex_t> shuffle_vs_execmaps_mutex;
//locks the pointer for the input vector
pthread_mutex_t input_vec_pointer_mutex = PTHREAD_MUTEX_INITIALIZER;
//mutex for the thread map
pthread_mutex_t thread_id_index_map_mutex = PTHREAD_MUTEX_INITIALIZER;
//mutex for starting the workers
pthread_mutex_t workers_await_creation_mutex = PTHREAD_MUTEX_INITIALIZER;
//the mutex over the shuffle cond-var
pthread_mutex_t is_more_work_for_shuffle_mutex = PTHREAD_MUTEX_INITIALIZER;
//----------------------------------------------------------
bool should_workers_start = false;
bool is_more_work_for_shuffle = false;
//----------------------------------------------------------
//Conditional for waking the shuffle, if any of the working threads finshes.
pthread_cond_t execmaps_shuffle_cv = PTHREAD_COND_INITIALIZER;
// cv for main thread to wait for others to finish
pthread_cond_t mainTh_wait_fo_workers = PTHREAD_COND_INITIALIZER;

pthread_cond_t workers_await_creation_cv = PTHREAD_COND_INITIALIZER;
//----------------------------------------------------------
// Global Mapreducebase for threads access
MapReduceBase *g_mapReduce;
//------------------------------------------------------------------------------
#ifdef DEBUG


pthread_mutex_t safe_print_mutex=PTHREAD_MUTEX_INITIALIZER;
#define DEBUG_SAFE(str)  \
pthread_mutex_lock(&safe_print_mutex);\
  std::cout <<"*"<<thread_id_index_map.at(pthread_self())<<": "<< str<<"*\n";\
 std::cout.flush();\
pthread_mutex_unlock(&safe_print_mutex);

#ifdef VV_DBG
#define DEBUG_SAFE_ASSUMPTIONS(str) DEBUG_SAFE(str)
#else
#define DEBUG_SAFE_ASSUMPTIONS(str) /**/
#endif

#else
#define DEBUG_SAFE(str) /**/
#define DEBUG_SAFE_ASSUMPTIONS(str) /**/
#endif

//------------------------------------------------------------------------------
//Define log for wrting the log to a file.
pthread_mutex_t safe_log_mutex = PTHREAD_MUTEX_INITIALIZER;
ofstream log_file;

#define MUTEXED_LOG(str)  \
pthread_mutex_lock(&safe_log_mutex);\
log_file << str << "\n";\
log_file.flush();\
pthread_mutex_unlock(&safe_log_mutex);
//------------------------------------------------------------------------------
/* Logger Functions */

double log_measure_start_time, log_measure_end_time;

string get_cur_parsed_time()
{
    string output_string;

    char buf[BUF_SIZE];
    time_t cur_raw_time;

    if (time(&cur_raw_time) == (-1))
    {
        output_string = "Error getting time";
    } else
    {
        struct tm *divided_time;
        divided_time = localtime(&cur_raw_time);

        if (strftime(buf, sizeof(buf), "[%d.%m.%Y %X]", divided_time) == 0)
        {
            output_string = "Error formatting time";
        } else
        {
            output_string = string(buf);
        }
    }

    return output_string;
}

string log_thread_action(string sender, string action)
{
    stringstream ss;
    ss << "Thread " << sender << " " << action << " " << get_cur_parsed_time();
    return ss.str();
}

string log_measure_time(string sender)
{
//Get time in milliseconds before the test.
    double result = ((log_measure_end_time - log_measure_start_time) *
                     MILI_TO_NANO);
    stringstream ss;
    ss << sender << " took " << result << " ns";

    return ss.str();
}

void log_set_time_start()
{
    struct timeval time;
    int result = gettimeofday(&time, NULL);
    if (result != FAIL_VAL) //Upon 'gettimeofday' failure.
    {
        log_measure_start_time = (double) (time.tv_sec * MILISEC_FACTOR +
                                           time.tv_usec);
    }
}

void log_set_time_end()
{
    struct timeval time;
    int result = gettimeofday(&time, NULL);
    if (result != FAIL_VAL) //Upon 'gettimeofday' failure.
    {
        log_measure_end_time = (double) (time.tv_sec * MILISEC_FACTOR +
                                         time.tv_usec);
    }
}

void log_init(int multiThreadLevel)
{
    log_file.open(".MapReduceFramework.log",
                  std::ios_base::app | std::ios_base::out);
    MUTEXED_LOG("runMapReduceFramework started with " << multiThreadLevel <<
                " threads")
}

void log_finalize()
{
    MUTEXED_LOG("runMapReduceFramework finished")
    log_file.close();
}

//Error handling Functions//----------------------------------------------------
void handle_error(string func_name)
{
    cerr << "MapReduceFramework Failure: " << func_name << " failed.\n";
    exit(1);
}
//------------------------------------------------------------------------------

//Processing Functions//--------------------------------------------------------
void *reduce_main(void *data)
{
    MUTEXED_LOG(log_thread_action("ExecReduce", "created"))
    //AWAIT CREATION -----------------------------------------------------------
    pthread_mutex_lock(&workers_await_creation_mutex);
    while (!should_workers_start)
    {
        pthread_cond_wait(&workers_await_creation_cv,
                          &workers_await_creation_mutex);
    }

    // This supresses warnings. sorry.
    int my_t = *((int *) data);
    while (0)
    { cout << my_t; }
    // end sorry


    DEBUG_SAFE("Reduce Thread #" << my_t << " Starts Working--->>");
    pthread_mutex_unlock(&workers_await_creation_mutex);
    //--------------------------------------------------------------------------

    //Do reduce work -----------------------------------------------------------
    MID_ITEMS_MAP::iterator chunk_start_marker;
    MID_ITEMS_MAP::iterator chunk_end_marker;


    while (true)
    {
        //Used to give the current reduce thread the number of info
        // (k2, v2 list*)
        //chunks to process.
        pthread_mutex_lock(&input_vec_pointer_mutex);
        chunk_start_marker = g_mid_items_map_shared_iterator;

        //The for loop is used to advance 'g_mid_items_map_shared_iterator'.
        for (int chunk_counter = 0; (chunk_counter < REDUCE_CHUNK_SIZE) && \
        (g_mid_items_map_shared_iterator != shuffle_output_map.end()); \
        ++g_mid_items_map_shared_iterator)
        { }

        chunk_end_marker = g_mid_items_map_shared_iterator;
        pthread_mutex_unlock(&input_vec_pointer_mutex);

        //proccess the chunks.
        while (chunk_start_marker != chunk_end_marker)
        {
            DEBUG_SAFE_ASSUMPTIONS(
                    "reducing: " << *(k2Imp *) (*chunk_start_marker).first)
            (*g_mapReduce).Reduce((*chunk_start_marker).first, \
                                  *(chunk_start_marker->second));
            chunk_start_marker++;
        }

        //Exit all threads when passed on all info chunks.
        if (g_mid_items_map_shared_iterator == shuffle_output_map.end())
        {
            break;
        }
    }
    DEBUG_SAFE("Exiting Reduce Thread #" << my_t << "...")
    //--------------------------------------------------------------------------
    pthread_exit(NULL);
}

void *shuffle_main(void *index)
{
    MUTEXED_LOG(log_thread_action("Shuffle", "created"))
    //AWAIT CREATION
    pthread_mutex_lock(&workers_await_creation_mutex);
    while (!should_workers_start)
    {
        pthread_cond_wait(&workers_await_creation_cv,
                          &workers_await_creation_mutex);
    }
    DEBUG_SAFE("SHUFFLE Starts Working --->>")
    int my_t = *((int *) index);
    pthread_mutex_unlock(&workers_await_creation_mutex);
    //---------------------------------------------------

//    cout /*<<dec<< hex*/ << my_t << "#";

    //Map shufle id with it' s addr  ess-------------------
    //TODO: Remove this part also from the shuffle thread function?
    //TODO: return p_thredt??
    unsigned long id = pthread_self();
    pthread_mutex_lock(&thread_id_index_map_mutex);
    // Add the (ID,index) pair to the map.
    thread_id_index_map.insert({id, my_t});
    pthread_mutex_unlock(&thread_id_index_map_mutex);
    //---------------------------------------------------

    //Init the time related vars: ---------------------------------
    //TODO: HOWTO milsecs instads of secs.
//    pthread_cond_t      cond  = PTHREAD_COND_INITIALIZER;
    int return_val;
    struct timespec tmspc;
    struct timeval tmval;
    bool shouldEnd = false;
    //--------------------------------------------------------------------------

    //on each shuffle wake this list will contain a union of the
    //lists generated by the execmaps so far.
    list<K2_V2_ITEM> shuffle_temp_list;

    while (true)
    {
        //Set a new timout value for the current shuffle run -------------------

        return_val = gettimeofday(&tmval, NULL);


        /* Convert from timeval to timespec */
        tmspc.tv_sec = tmval.tv_sec;
        tmspc.tv_nsec = (tmval.tv_usec * MICRO_SEC_TO_NANO_SEC) + \
                                                             WAIT_TIME_INTERVAL;
        //----------------------------------------------------------------------

        //Wait for maps: -------------------------------------------------------
        pthread_mutex_lock(&is_more_work_for_shuffle_mutex);
        DEBUG_SAFE("Shuffle blocked (waiting for signal from workers)");
        return_val = pthread_cond_timedwait(&execmaps_shuffle_cv,
                                            &is_more_work_for_shuffle_mutex,
                                            &tmspc);
        pthread_mutex_unlock(&is_more_work_for_shuffle_mutex);
        DEBUG_SAFE("Shuffle released");

        //Wait for maps result.
        if (return_val == ETIMEDOUT)
        {
            DEBUG_SAFE("Shuffle timed out!");
        } else if (return_val == 0)
        { //Success.
            DEBUG_SAFE("shuffle got signaled")
        } else
        {
            DEBUG_SAFE("failed conditional wait for shuffle")
        }
        //----------------------------------------------------------------------

        //Close all threads and finish -----------------------------------------
        if (g_input_list_shared_iterator == g_input_list.end())
        {
            DEBUG_SAFE(
                    "Last chunk of info were given, when all threads finish, "
                            "shuffle should end.")
            for (int t = EXEC_MAP_STARTING_INDEX; t < real_thread_num; t++)
            {
                void *status;
                DEBUG_SAFE("Joining worker thread #" << t)
                int res = pthread_join(threads[t], &status);
                if (res < 0)
                {
                    DEBUG_SAFE("ERROR when workers joining\n");
                    handle_error("pthread_join");

                }
                MUTEXED_LOG(log_thread_action("ExecMap", "terminated"))
            }
            shouldEnd = true;
        }
        //----------------------------------------------------------------------

        //Each shuffle run, takes the (k2,v2) (after mapping), the puts them in
        //a temp list ----------------------------------------------------------

        for (int thread_id = EXEC_MAP_STARTING_INDEX;
             thread_id < real_thread_num; ++thread_id)
        {
            //try to get lock to the shared data dump of the exec maps
            int res = pthread_mutex_trylock(
                    &shuffle_vs_execmaps_mutex[thread_id]);
            if (res == SUCCESS)
            {
                DEBUG_SAFE("Shuffle is working on thread #: " << thread_id)
                //merge the temp working list with the
                // currently-iterated-thread's list
                shuffle_temp_list.merge(
                        *(emit2_k2v2_output_container[thread_id]));

                //clear the currently-iterated-thread's list
                (*(emit2_k2v2_output_container[thread_id])).clear();

                pthread_mutex_unlock(&shuffle_vs_execmaps_mutex[thread_id]);
            } else
            {
                //else no work to do for this threads container
                DEBUG_SAFE(
                        "couldn't lock t:" << thread_id << " reason: " << res)
            }
        }
        //----------------------------------------------------------------------

        //Ordering (k2,v2) using a multi-map -----------------------------------
        //Do shuffle work here (the multi-map takes care of multiple values
        // per key),
        //by using the order of 'ltk2base'.
        for (auto k2v2 : shuffle_temp_list)
        {
            MID_ITEMS_MAP::iterator it = shuffle_output_map.find(k2v2.first);



            if (it != shuffle_output_map.end())
            {

                //(it->second) -> push_back(k2v2.second);
                shuffle_output_map[k2v2.first]->push_back(k2v2.second);
            } else
            {
                try
                {
                    shuffle_output_map[k2v2.first] = new V2_LIST();
                    shuffle_output_map[k2v2.first]->push_back(k2v2.second);
                } catch (bad_alloc e)
                {
                    handle_error("new");
                }
            }

        }
        DEBUG_SAFE("shuffle output size: " << shuffle_output_map.size() <<
                   " shuffle temp size: " << shuffle_temp_list.size())
        //now empty the temp list
        shuffle_temp_list.clear();
        //----------------------------------------------------------------------

        //All info chunks finished and threads ended (joined)--------------------
        if (shouldEnd)
        {
            break;
        }
        //----------------------------------------------------------------------
    }
    DEBUG_SAFE("Exiting Shuffle...")

    pthread_cond_signal(&mainTh_wait_fo_workers);
    pthread_exit(NULL);
}
void *exec_map_main(void *index)
{
    MUTEXED_LOG(log_thread_action("ExecMap", "created"))
    //AWAIT CREATION
    pthread_mutex_lock(&workers_await_creation_mutex);
    while (!should_workers_start)
    {
        pthread_cond_wait(&workers_await_creation_cv,
                          &workers_await_creation_mutex);
    }
    int my_t = *((int *) index);
//    unsigned long id = pthread_self();
//    if(thread_id_index_map.find(id)==thread_id_index_map.end()){
//        DEBUG_PRINT("/"<<endl);
//    }

    DEBUG_SAFE("Thread #" << my_t << " Starts Working --->>");
    pthread_mutex_unlock(&workers_await_creation_mutex);

    //---------------------------------------------------

    K1_LIST_ITER chunk_start;
    K1_LIST_ITER chunk_end;
    bool should_I_terminate = false;

    while (shared_index < g_list_length)
    {
        //First Lock the shared pointer
        //K1 List critical section ////////////////////////////
        pthread_mutex_lock(&input_vec_pointer_mutex);
        chunk_start = g_input_list_shared_iterator;
        //check if list did not end
        if (g_input_list_shared_iterator != g_input_list.end())
        {
            //advance the iterator
            for (int i = 0; i < MAP_CHUNK_SIZE; i++)
            {
                g_input_list_shared_iterator++;
                if (g_input_list_shared_iterator == g_input_list.end())
                {
                    break;
                }
            }

            if (g_input_list_shared_iterator == g_input_list.end())
            {
                DEBUG_SAFE("The shared iterator is at the end()");
            }
            shared_index += MAP_CHUNK_SIZE;
            DEBUG_SAFE("shared index was advanced to:  " << shared_index);
            chunk_end = g_input_list_shared_iterator;
        } else
        {
            //we are at the end of the list, terminate map.
            should_I_terminate = true;
        }
        pthread_mutex_unlock(&input_vec_pointer_mutex);
        ////END Crit Sec////////////////////////////////////

        //No more info chunks to give to the current thread.
        if (should_I_terminate)
        {
            break;
        }

        //Else:
        //This locks access to the shared data dump of the exec maps with
        // the shuffle.
        pthread_mutex_lock(&shuffle_vs_execmaps_mutex[my_t]);
        //Do map work here!
        DEBUG_SAFE("got lock on mutex vs shuffle, starting work...")
        for (; chunk_start != chunk_end; chunk_start++)
        {
            //First and Second values of the tuple (k1, v1).
            DEBUG_SAFE_ASSUMPTIONS(
                    "Mapping: " << *(k1Imp *) (*chunk_start).first)

            (*g_mapReduce).Map((*chunk_start).first, (*chunk_start).second);
        }

        DEBUG_SAFE("unlocked - signaling shuffle.")
        //wake up shuffle thread afterwards.
        pthread_cond_signal(&execmaps_shuffle_cv);
        pthread_mutex_unlock(&shuffle_vs_execmaps_mutex[my_t]);
    }
    DEBUG_SAFE("Exiting Worker Thread #" << my_t << "...")
    pthread_cond_signal(&mainTh_wait_fo_workers);
    pthread_exit(NULL);
}

OUT_ITEMS_LIST runMapReduceFramework(MapReduceBase &mapReduce,
                                     IN_ITEMS_LIST &itemsList,
                                     int multiThreadLevel)
{
    if (multiThreadLevel < 1)
    {
        exit(-1);
    }

    real_thread_num = multiThreadLevel + 1;

#ifdef LOG_TO_FILE
    std::ofstream out("out.txt");
//    std::streambuf *coutbuf = std::cout.rdbuf(); //save old buf
    std::cout.rdbuf(out.rdbuf()); //redirect std::cout to out.txt!
#endif


    DEBUG_PRINT("[~~~~~~~~~REAL~~~~~~~~~]")
    log_init(multiThreadLevel);
    log_set_time_start();
    string reduce_time, map_n_shuffle_time; //For log time measurements.

    DEBUG_PRINT(endl << "=== runMapReduceFramework start ===" << endl)

    //First map the id of the calling thread to "CALLING_THREAD_ID"
    unsigned long id = pthread_self();

    thread_id_index_map.insert({id, CALLING_THREAD_ID});

    //set the global map reduce
    g_mapReduce = &mapReduce;

    //We want to keep a copy of the input list for debuging/testing purposes
    std::copy(itemsList.begin(), itemsList.end(),
              std::back_inserter(g_input_list));

    g_list_length = itemsList.size();
    g_input_list_shared_iterator = g_input_list.begin();

    // init the monitor vector pointer
    pthread_cond_t vec_index_monitor;
    pthread_cond_init(&vec_index_monitor, NULL);

    //this is just a holder for the indices of the threads from the perspective
    //of the main thread
    vector<int> datas;
    int res, t;

    //Creates the first thread with the "Shuffle" logic (the "consumer")
    //as thread number '0'.
    DEBUG_PRINT("Initing data structs\n")

    //--------------- FIRST: init all the data structs-------------
    for (t = 0; t < real_thread_num; t++)
    {
        datas.push_back(t);
        //make sure vector has enough cells
        threads.push_back(0);
        //init mutexes
        shuffle_vs_execmaps_mutex.push_back(PTHREAD_MUTEX_INITIALIZER);
        //create a new list and put in global vector

        try
        {
            shared_ptr<list<K2_V2_ITEM>> ptr1(new list<K2_V2_ITEM>());
            shared_ptr<list<OUT_ITEM>> ptr2(new list<OUT_ITEM>());
            emit2_k2v2_output_container.push_back(move(ptr1));
            g_reduce_output_container.push_back(move(ptr2));
        } catch (bad_alloc e)
        {
            handle_error("new");
        }

    }

    //Threads Creation----------------------------------------------------------
    //Create the shuffle thread.
    DEBUG_PRINT("Creating shuffle thread\n")
    if (pthread_create(&threads[0], NULL, shuffle_main, (void *) &(datas[0])) <
        0)
    {
        handle_error("pthread_create");
    } else
    {
        // Add the (ID,index) pair to the map.
        thread_id_index_map.insert({threads[0], 0});
    }

    //Create the worker threads.
    for (t = EXEC_MAP_STARTING_INDEX; t < real_thread_num; t++)
    {
        DEBUG_SAFE("Creating worker thread #" << t);
        res = pthread_create(&threads[t], NULL, exec_map_main,
                             (void *) &(datas[t]));
        if (res < 0)
        {
            handle_error("pthread_create");
        } else
        {
            thread_id_index_map.insert({threads[t], t});
        }
    }
    //--------------------------------------------------------------------------

    //Wait all threads to be created, than starts their work -------------------
    DEBUG_SAFE("Shuffle Workers Go!")
    //Takes control over the mutex, after all of the worker threads released it
    //and went to waiting state.
    pthread_mutex_lock(&workers_await_creation_mutex);
    should_workers_start = true;
    //Signal all threads that they can proceed from waiting state. The loop with
    //cond-wait will finish and exit with 'should_workers_start=true'.
    pthread_cond_broadcast(&workers_await_creation_cv);
    pthread_mutex_unlock(&workers_await_creation_mutex);
    //--------------------------------------------------------------------------

    //Main thread is done - Join the shuffle thread ----------------------------
    DEBUG_SAFE("Joining thread #0 (Start shuffling).");
    res = pthread_join(threads[0], &status);
    if (res < 0)
    {
        DEBUG_PRINT("ERROR when joining\n");
        handle_error("pthread_join");
    }

    MUTEXED_LOG(log_thread_action("Shuffle", "terminated"))
    log_set_time_end();
    map_n_shuffle_time = log_measure_time("Map and Shuffle");

    DEBUG_SAFE("Shuffling Finished.");
    //--------------------------------------------------------------------------

    //Now shuffle and maps are done.
    //Start Reducing, FIRST:  --------------------------------------------------
    log_set_time_start();

    //refresh some values need for reducers
    should_workers_start = false;
    shared_index = 0; //For next info chunks iterations.
    thread_id_index_map.clear();
    thread_id_index_map.insert({id, CALLING_THREAD_ID});
    g_mid_items_map_shared_iterator = shuffle_output_map.begin();
    //decrement the num of threads before reduce
    real_thread_num--;

    //REDUCE Threads Creation, init data structures and allocate memory---------
    for (int t = 0; t < real_thread_num; t++)
    {

        DEBUG_PRINT("Creating reduce thread #" << t << endl);
        res = pthread_create(&threads[t], NULL, reduce_main,
                             (void *) &(datas[t]));
        if (res < 0)
        {
            DEBUG_PRINT("ERROR\n");
            handle_error("pthread_create");
        } else
        {
            thread_id_index_map.insert({threads[t], t});
        }
    }
    //--------------------------------------------------------------------------

    //Wait all threads to be created, than starts their work -------------------
    DEBUG_SAFE("Reduce Workers Go!")
    //Takes control over the mutex, after all of the worker threads released it
    //and went to waiting state.
    pthread_mutex_lock(&workers_await_creation_mutex);
    should_workers_start = true;
    //Signal all threads that they can proceed from waiting state. The loop with
    //cond-wait will finish and exit with 'should_workers_start=true'.
    pthread_cond_broadcast(&workers_await_creation_cv);
    pthread_mutex_unlock(&workers_await_creation_mutex);
    //--------------------------------------------------------------------------

    //Close all threads and finish ---------------------------------------------
    DEBUG_SAFE("before joining the reduce threads")
    //Join all threads.
    for (int t = 0; t < real_thread_num; t++)
    {
        void *status;
        DEBUG_SAFE("Joining reduce thread #" << t)
        int res = pthread_join(threads[t], &status);
        if (res < 0)
        {
            DEBUG_SAFE("ERROR when reduce joining\n");
            handle_error("pthread_join");
        }
        MUTEXED_LOG(log_thread_action("ExecReduce", "terminated"))
    }

//------------------------------------------------------------------------------

    DEBUG_SAFE(
            "[~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~END THREADS"
                    "~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~]")

//Create the "flat" k3v3 list and sort it by KEY -------------------------------
    OUT_ITEMS_LIST k3v3_final_lst;

    for (int t = 0; t < real_thread_num; t++)
    {
        for (OUT_ITEM list_node : *(g_reduce_output_container[t]))
        {
            (k3v3_final_lst).push_back(list_node);
        }
    }

    //Sort with custom lambda expression.
    (k3v3_final_lst).sort([](const K3_V3_ITEM &a, const K3_V3_ITEM &b) {
        return *a.first < *b.first;
    });
//------------------------------------------------------------------------------

    log_set_time_end();
    reduce_time = log_measure_time("Reduce");

    MUTEXED_LOG(map_n_shuffle_time)
    MUTEXED_LOG(reduce_time)
    log_finalize();
    DEBUG_SAFE("Free memory")
    //Free memory and clear data structs:
    thread_id_index_map.clear();
    datas.clear();
    g_input_list.clear();
    shared_index = 0;
    shuffle_vs_execmaps_mutex.clear();
    for (int t = 0; t < real_thread_num; ++t)
    {
        //free reduce containers --------------------
        auto ptr_rdc = g_reduce_output_container[t];
        ptr_rdc->clear();

        // free exec map lists --------------------
        auto ptr_map = emit2_k2v2_output_container[t];
        for (auto elem:*ptr_map)
        {
            delete elem.first;
            delete elem.second;
        }
        ptr_map->clear();

    }
    for (auto ptr_sff : shuffle_output_map)
    {
        for (auto const &elm:*ptr_sff.second)
        {
            delete elm;
        }
        (ptr_sff.second)->clear();
        delete ptr_sff.second;
    }
    shuffle_output_map.clear();
    should_workers_start = false;
    DEBUG_PRINT(
            "~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~return from run()"
                    "~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\n")
    return k3v3_final_lst;
}

void Emit2(k2Base *k, v2Base *v)
{
    if (!IS_COMPARE)
    {

        //get the index
        unsigned long id = pthread_self();
        int idx;
        try
        {
            idx = thread_id_index_map.at(id);

            if (idx == CALLING_THREAD_ID)
            {
                DEBUG_PRINT("CALLING_THREAD_ID shouldnt run maps!")
            } else
            {

                DEBUG_SAFE("emitting-2")
                auto ptr_k2v2_lst = (emit2_k2v2_output_container[idx]);
                DEBUG_SAFE(ptr_k2v2_lst->size());
                (*ptr_k2v2_lst).push_back({k, v});
                DEBUG_SAFE("emitted")
            }
        } catch (const std::out_of_range e)
        {
            DEBUG_PRINT(e.what() << endl)
            DEBUG_PRINT("id was: " << id << endl)
            handle_error("Emit2");
        }
    }

}

void Emit3(k3Base *k, v3Base *v)
{

    //get the index
    unsigned long id = pthread_self();
    int idx;
    try
    {
//        idx= thread_id_index_map.at(id);
        int count = thread_id_index_map.count(id);
        if (count == 0)
        {

            DEBUG_SAFE(id << "*")
        } else
        {
            idx = thread_id_index_map.at(id);
        }
        if (idx == CALLING_THREAD_ID)
        {
            DEBUG_PRINT("CALLING_THREAD_ID shouldnt run maps!")
        } else
        {

//        DEBUG_PRINT(id << " " << idx << " insert: ")
            (*(g_reduce_output_container[idx])).push_back({k, v});
        }
    } catch (const std::out_of_range e)
    {
        DEBUG_PRINT(e.what() << endl)
        DEBUG_PRINT("id was: " << id << endl)
        handle_error("Emit3");
    }
}

#endif