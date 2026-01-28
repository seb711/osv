/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include "drivers/clockevent.hh"
#include "drivers/clock.hh"
#include "exceptions.hh"
#include "apic.hh"

using namespace processor;

class apic_clock_events : public clock_event_driver {
public:
    explicit apic_clock_events();
    ~apic_clock_events();
    virtual void setup_on_cpu();
    virtual int get_vector();
    virtual void set_periodic(bool b);  
    virtual void set(std::chrono::nanoseconds nanos);
    virtual void reset_vector(unsigned vector); 
    virtual void disable(); 
    unsigned _vector;
    bool _is_periodic = false; 
private:
};

apic_clock_events::apic_clock_events()
    : _vector(idt.register_handler([this] { _callback->fired(); }))
{
}

apic_clock_events::~apic_clock_events()
{
}

void apic_clock_events::setup_on_cpu()
{
    processor::apic->write(apicreg::TMDCR, 0xb); // divide by 1
    processor::apic->write(apicreg::TMICT, 0);
    processor::apic->write(apicreg::LVTT, _vector); // one-shot
}

void apic_clock_events::set_periodic(bool b) {
    if (!b) {
        processor::apic->write(apicreg::LVTT, _vector); // one-shot
        _is_periodic = false; 
    } else {
        processor::apic->write(apicreg::LVTT, _vector | 0x20000); // periodic
        _is_periodic = true; 
    }
}

void apic_clock_events::reset_vector(unsigned vector) {
    // printf("set new interrupt vector on apic clock %d\n", vector); 
    _vector = vector;
    setup_on_cpu(); 
}

int apic_clock_events::get_vector() {
    return (int) _vector; 
}

void apic_clock_events::disable() {
    processor::apic->write(apicreg::TMICT, 0);
}

void apic_clock_events::set(std::chrono::nanoseconds nanos)
{
    if (nanos.count() <= 0) {
        _callback->fired();
    } else {
        // FIXME: handle overflow
        /* if (_vector == 42) {
            printf("nanos: %d\n", nanos.count()); 
        } */
        apic->write(apicreg::TMICT, nanos.count());
    }
}


void __attribute__((constructor)) init_apic_clock()
{
    // FIXME: detect
    clock_event = new apic_clock_events;
}
