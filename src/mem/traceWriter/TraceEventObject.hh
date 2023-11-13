#ifndef __TRACE_EVENT_OBJECT_HH__
#define __TRACE_EVENT_OBJECT_HH__

#include "DefaultTrace/DefaultTraceWriter.hh"
#include "GenericTraceWriter.hh"
#include "base/types.hh"
#include "mem/memory_content.hh"
#include "params/TraceEventObject.hh"
#include "sim/eventq.hh"
#include "sim/sim_object.hh"
#include <string>
#include <vector>

namespace gem5
{

class TraceEventObject : public SimObject
{
  private:
    void processEvent();

    EventFunctionWrapper event;  

    std::ofstream trace;
    
    std::string filePath = "test.txt";

    std::vector<memory_content> buffer;

    DefaultTraceWriter *tracer = new DefaultTraceWriter();

    const Tick time;

    bool flag = 1;

    int index;

  public:
    TraceEventObject(const TraceEventObjectParams &p);

    int getIndex();

    bool getFlag();

    void setFlag();

    void resetFile();

    void addIndex();

    void startup() override;

    void insertBuffer(memory_content c);
};

} // namespace gem5

#endif // __TRACE_EVENT_OBJECT_HH__