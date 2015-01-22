#ifndef __SG_UNIQUE_UNITTEST_H__
#define __SG_UNIQUE_UNITTEST_H__

#include <shogun/base/unique.h>

#ifdef HAVE_CXX11

class SomeTestingClassWithUnique
{
private:
    class Self;
    shogun::Unique<Self> self;   
};

#endif

#endif
