#include <iostream>
#ifndef _LOGGER_H
#define _LOGGER_H
//source found from: http://coliru.stacked-crooked.com/a/1a03f2095f0ac308
class Log {

    public:
        enum msg_type {
            dbg =1,
            inf,
            wrn,
            err
        };
        bool enabled = false;

        Log(enum msg_type mt, bool is_enabled);

        template<typename T>
        Log& operator<<(const T& t)
        {
            if(enabled) {
                std::cout << t;
            }
            return *this;
        }
};
#endif // LOGGER.h