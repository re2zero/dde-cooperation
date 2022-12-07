#ifndef INPUTEMITOR_H
#define INPUTEMITOR_H

#include <string>
#include <memory>

#include <libevdev/libevdev-uinput.h>

#include "common.h"

class InputEmittor {
public:
    explicit InputEmittor(InputDeviceType type);
    ~InputEmittor();

    bool emitEvent(unsigned int type, unsigned int code, int value);

private:
    std::unique_ptr<libevdev, decltype(&libevdev_free)> m_dev;
    std::unique_ptr<libevdev_uinput, decltype(&libevdev_uinput_destroy)> m_uidev;

    void enableEventType(unsigned int type);
    void enableEventCode(unsigned int type, unsigned int code, const void *data = nullptr);
};

#endif // !INPUTEMITOR_H
