//
// Created by dos884 on 3/31/16.
//

#ifndef OS_EX3_ENVCONTROLLER_H
#define OS_EX3_ENVCONTROLLER_H

#include <pthread.h>
#include "MapReduceClient.h"

//#define OS_2016_EX3_MACRO_DUMMY
//CHOOSE which framework to use
#ifdef OS_2016_EX3_MACRO_DUMMY
#include "DummyMapReduceFramework.h"
#else
#include "MapReduceFramework.h"
#endif




//#include <list>
//#include <iostream>

#include "MapReduceClient.h"

//#define VV_DBG
//#define DEBUG
//#define LOG_TO_FILE
///////////////////////////////////////////////////////////////
//Printing funcs and macros used for Debug//

#ifdef DEBUG


extern void * shared_debug_data;

#define DEBUG_PRINT(str)  std::cout << str; std::cout.flush();
#define PRINT_OBJ(obj) DEBUG_PRINT( " obj{'"<< *((string*) (obj).data )<<"', @ "<<&obj<< "} "  )
#define DEBUG_LIST(list) /**/
#else
#include "MapReduceClient.h"
#define DEBUG_PRINT(str) //do { } while ( false )
#define DEBUG_LIST(list)
#define DEBUG_ALL
#endif

#define LOG_THIS(str) DEBUG_PRINT(str)




///////////// End debug functions/////////////////////
////////////////////////////////////////////////////
#endif //OS_EX3_ENVCONTROLLER_H
