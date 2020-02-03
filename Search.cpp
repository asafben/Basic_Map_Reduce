#include <iostream>
#include <string>
#include <cstring>
#include <typeinfo>
#include <sstream>
#include <dirent.h>
//#include <climits>
//#include <cstdlib>

#include "EnvController.h"
#include "DebugClient.h"
#include "dirent.h"

#define VAL_TO_SEARCH_INDEX 1
#define NUM_OF_THREADS 5
#define COUNT_VAL 1
#define START_PARAM_INDEX
using namespace std;

//CHOOSE which framework to use
#ifdef OS_2016_EX3_MACRO_DUMMY
#include "DummyMapReduceFramework.h"
#else

#include "MapReduceFramework.h"

#endif
using namespace std;

class MapReduceSearch : public MapReduceBase {
public:
    void Map(const k1Base *const key, const v1Base *const val) const override
    {
        DIR *dir;
        struct dirent *ent;
        char *dir_path = (char *) (((k1Imp *) key)->data);
        k2Imp *file_name;
        v2Imp *search_val;

        //just to supress warning..
        while (false)
        { cout << val; }
        //end supress
        if ((dir = opendir(dir_path)) != NULL)
        {

            DEBUG_PRINT("\nxxxxxxxxxxxxxxxxx Legal path opened: " << dir_path <<
                        " xxxxxxxxxxxxxxxxx\n");
            while ((ent = readdir(dir)) != NULL)
            {
                //Always readdir returns these values, ignored.
                if ((string(ent->d_name).compare(".") == 0) ||
                    (string(ent->d_name).compare("..") == 0))
                {
                    continue;
                }

                file_name = new k2Imp();
                search_val = new v2Imp();
                file_name->data = new string(ent->d_name);
                search_val->data = new string("1");

                DEBUG_PRINT("The current file name (key) is: " <<
                            *((string *) (file_name->data)) <<
                            ", and the value given to it is : " <<
                            *((string *) (search_val->data)) << ".\n")

                Emit2(file_name, search_val);
            }

            closedir(dir);
            DEBUG_PRINT("xxxxxxxxxxxxxxxxx Finished with: " << dir_path <<
                        " xxxxxxxxxxxxxxxxx\n");

        } else
        {
            /* could not open directory */
            DEBUG_PRINT("Invalid path, skipped." << "\n");
        }
    }

    void Reduce(const k2Base *const key, const V2_LIST &vals) const
    {

        //V2_LIST::const_iterator lst_it;
        string *k2_filename = (string *) (((k2Imp *) key)->data);
        k3Imp *file_name = new k3Imp();
        v3Imp *num_of_occurrences = new v3Imp();

        //for(lst_it = vals.begin();  lst_it != vals.end();  ++lst_it)
        //{
        (file_name->data) = k2_filename;
        (num_of_occurrences->data) = new string(to_string(vals.size()));

        DEBUG_PRINT("filename: " << *((string *) (file_name->data)) <<
                    ", Num of instances: " <<
                    *((string *) (num_of_occurrences->data)) << ".\n")

        Emit3(file_name, num_of_occurrences);
        //}
    }

};

int main(int argc, char *argv[])
{
    //Define variables.
    MapReduceSearch *map_n_reduce = new MapReduceSearch();
    k1Imp *key;
    v1Imp *val;

    IN_ITEMS_LIST input_dirent_pairs_list;
    IN_ITEM temp_pair;
    OUT_ITEMS_LIST num_of_files_per_name_list;

    bool at_least_one_match = false; //For better result printing.

    //Deal with arguments input.
    if (argc < 3)
    {
        cerr << "Usage: <substring to search> <folders, separated by space>" <<
        endl;
        exit(0);
    }

    string str_to_search = (const char *) argv[VAL_TO_SEARCH_INDEX];

    DEBUG_PRINT("Searching for file names containging the string: " <<
                str_to_search << "\n\n")

    //Create k1v1 list.
    for (int path_index = 2; path_index < argc; path_index++)
    {
        DEBUG_PRINT(
                "Current directory string from cmd is: " << argv[path_index] <<
                "\n")
        key = new k1Imp;
        val = new v1Imp;

        key->data = argv[path_index];
        val->data = NULL;
        temp_pair = {key, val};
        input_dirent_pairs_list.push_back(temp_pair);

        DEBUG_PRINT("\n");
    }

    //Run map & reduce.
    num_of_files_per_name_list = runMapReduceFramework(*map_n_reduce,
                                                       input_dirent_pairs_list,
                                                       NUM_OF_THREADS);

    //Process final output.
    DEBUG_PRINT("\n\n=== MapReduce Results ===" << endl)
    for (OUT_ITEM k3v3_item : num_of_files_per_name_list)
    {
//        k3Base* k3 = (v3Imp*)k3v3_item.first;
        string file_name = *((string *) (((k3Imp *) k3v3_item.first)->data));

        //If there is a match.
        if (file_name.find(str_to_search) != std::string::npos)
        {
            at_least_one_match = true;

            int num_of_prints = stoi(
                    *((string *) (((v3Imp *) k3v3_item.second)->data)));
            for (int i = 0; i < num_of_prints; i++)
            {
                cout << file_name << " ";
            }
        }
    }
    if (!at_least_one_match)
    {
        cout << "No matches.";
    }
    DEBUG_PRINT("\n=== END ===")

    return 0;
}