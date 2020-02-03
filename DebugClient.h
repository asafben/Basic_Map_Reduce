//
// Created by dos884 on 4/19/16.
//

#ifndef OS_EX3_DEBUGCLIENT_H
#define OS_EX3_DEBUGCLIENT_H

#include <string>

#include "MapReduceClient.h"

using namespace std;

class k1Imp : public k1Base {
public:

    void *data;

    bool operator<(const k1Base &other) const
    {
        //dont use this function!
        exit(1);
        return &other == this;
    }

    friend inline std::ostream &operator<<(std::ostream &strm, const k1Imp &b)
    {
        return strm << *((string *) b.data);
    }

    string to_string()
    {
        return *((string *) this->data);
    }

};

class v1Imp : public v1Base {
public:
    void *data;
};

class k2Imp : public k2Base {
public:

    void *data;

    bool operator<(const k2Base &other) const
    {

        bool ret = *((string *) (this->data)) <
                   *((string *) (((const k2Imp &) (other)).data));

        return ret;

    }

    friend inline std::ostream &operator<<(std::ostream &strm, const k2Imp &b)
    {
        return strm << *((string *) b.data);
    }
};

class v2Imp : public v2Base {
public:

    void *data;
};

class k3Imp : public k3Base {
public:
    void *data;

    bool operator<(const k3Base &other) const
    {

        bool ret = *((string *) (this->data)) <
                   *((string *) (((const k2Imp &) (other)).data));


        return ret;
    }

    friend inline std::ostream &operator<<(std::ostream &strm, const k3Imp &b)
    {
        return strm << *((string *) b.data);
    }
};

class v3Imp : public v3Base {
public:
    void *data;
};


#endif //OS_EX3_DEBUGCLIENT_H
