#include "TraceEventObject.hh"
#include "base/types.hh"
#include "params/SimObject.hh"
#include "params/TraceEventObject.hh"
#include "sim/cur_tick.hh"
#include <zlib.h>
#include <cstddef>
#include <ctime>
#include <ios>
#include <iostream>
#include <iterator>



namespace gem5
{

TraceEventObject::TraceEventObject(const TraceEventObjectParams &params) :
    SimObject(params),
    event([this]{processEvent();}, 
    name()), 
    time(params.timer)

{
    std::cout << "Created TraceObject";
}

int
TraceEventObject::getIndex()
{
    return index;
}

bool
TraceEventObject::getFlag() 
{
    return this->flag;
}

void
TraceEventObject::setFlag()
{
    this->flag = 0;
}

void 
TraceEventObject::resetFile()
{
    trace.open(filePath);
    trace.close();
}

void 
TraceEventObject::processEvent()
{
    if(!buffer.empty()) {
        trace.open(filePath, std::ios::out | std::ios::app);
        for (int i = 0; i<buffer.size(); i++) {
            tracer->WriteTraceLine(trace, buffer.at(i));
        }
        buffer.erase(buffer.begin(), buffer.begin()+buffer.size()-1);
        trace.close();
    }
    schedule(event, curTick() + time);
}

void
TraceEventObject::startup()
{
    buffer.clear();
    resetFile();
    schedule(event, curTick() + time);
}

void
TraceEventObject::addIndex()
{
    index++;
}

void
TraceEventObject::insertBuffer(memory_content c)
{   
    buffer.push_back(c);
}

} //namespace gem5