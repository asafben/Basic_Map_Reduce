.PHONY = clean all


CXX = g++ -std=c++11
FLAGS = -Wextra -Wall
FILES_TO_CREATE =  Search tar
FILES_TO_DELETE = *.o  MapReduceFramework.a main_bin ex3.tar
FILES_FOR_TAR = MapReduceFramework.cpp EnvController.h Search.cpp DebugClient.h Makefile README
MAIN_FILE = bnaya_tests
# era_tests , main , search , bnaya_tests
submit: MapReduceFramework.a Search

# COMPAREMapReduceFramework.cpp, COMPAREMapReduceFramework.h, MapReduceFramework.cpp, DummyMapReduceFramework.cpp, DummyMapReduceFramework.h, EnvController.h, Search.cpp, DebugClient.h, 
main: MapReduceFramework.a $(MAIN_FILE).o
		$(CXX) -g $(MAIN_FILE).o -L. MapReduceFramework.a -o main_bin -pthread


MapReduceFramework.a:  MapReduceFramework.o
		  			   ar rcs MapReduceFramework.a  MapReduceFramework.o
		  				
MapReduceFramework.o: MapReduceFramework.h MapReduceClient.h MapReduceFramework.cpp
					  $(CXX) -c -g $(FLAGS) EnvController.h DebugClient.h MapReduceFramework.cpp -pthread

$(MAIN_FILE).o: $(MAIN_FILE).cpp  MapReduceFramework.h
		  $(CXX) -c -g $(FLAGS) $(MAIN_FILE).cpp -pthread

Search.o: Search.cpp EnvController.h DebugClient.h MapReduceFramework.h
	$(CXX) -c $(FLAGS) Search.cpp -pthread

Search: MapReduceFramework.a Search.o
	$(CXX) -g Search.o -L. MapReduceFramework.a -o Search -pthread

# valgrind:main_bin
# 	valgrind main_bin check /cs/stud/elyasaf/safe/Operating-Systems/ex3/TEST check /cs/stud/elyasaf/safe/Operating-Systems/ex3/TEST2 check /cs/stud/elyasaf/safe/Operating-Systems/ex3/TEST3

# valgrindfull:main_bin
# 	valgrind --leak-check=full main_bin check /cs/stud/elyasaf/safe/Operating-Systems/ex3/TEST check /cs/stud/elyasaf/safe/Operating-Systems/ex3/TEST2 check /cs/stud/elyasaf/safe/Operating-Systems/ex3/TEST3

all: $(FILES_TO_CREATE)

			
tar: $(FILES_FOR_TAR)
	 tar cvf ex3.tar $(FILES_FOR_TAR)
	   
			
clean:
	 rm $(FILES_TO_DELETE) Search