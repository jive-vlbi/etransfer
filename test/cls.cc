#include <iostream>

using std::cout;
using std::endl;

struct Base {

    virtual ~Base() { }

    const bool isDir;

    protected:
        Base(bool x): isDir(x) {}
};

struct File : public Base {
    File(): Base(false) {}
};

struct Dir : public Base {
    Dir(): Base(true) {}
};

int main( void ) {
    File  f;
    Dir   d;

    cout << "File: isDir=" << f.isDir << endl;
    cout << "Dir : isDir=" << d.isDir << endl;
    return 0;
}
