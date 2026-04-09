/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation;
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "ns3/object.h"
#include "ns3/simulator.h"
#include "ns3/trace-source-accessor.h"
#include "ns3/traced-value.h"
#include "ns3/uinteger.h"

#include <iostream>

using namespace ns3;

/**
 * Tutorial 4 - tracing basics with a custom ns-3 Object.
 *
 * What tracing means in ns-3:
 * - Tracing is ns-3's event-observation mechanism: simulation objects expose
 *   internal state changes (or events) as "trace sources".
 * - User code can connect callback functions (called sinks) to those sources
 *   to observe what happened, when it happened, and values before/after change.
 * - Common uses include debugging protocol behavior, collecting metrics,
 *   building custom logs, exporting traces for post-processing, and validating
 *   model assumptions without changing the model's core logic.
 *
 * In real simulations, tracing can be used with:
 * - direct callback connections (TraceConnect...)
 * - helper-generated files (pcap/ascii tracing in many helpers)
 * - Config paths to connect to trace sources on many objects at once.
 *
 * What this file demonstrates:
 * - Define a custom trace source on a user Object.
 * - Connect a callback to that source.
 * - Trigger the source by changing a traced value.
 * - Observe old/new values printed from the callback.
 */
class MyObject : public Object
{
  public:
    /**
         * Register this class in ns-3 TypeId system.
         *
         * Why inherit from Object:
         * - Classes that need runtime type information, attributes, trace sources,
         *   and reference-counted lifetime should inherit from ns3::Object.
         * - Lightweight data-only helpers do not always need Object; but if you
         *   want AddTraceSource/AddAttribute/TypeId-based configuration, Object is
         *   the standard base class.
         *
         * AddTraceSource("MyInteger", ...):
         * - Name: "MyInteger" is the key used when connecting callbacks.
         * - Help string: explains what the source represents.
         * - Accessor: points to the member variable that emits trace events
         *   (here m_myInt, a TracedValue<int32_t>).
         * - Callback signature string: describes expected sink signature;
         *   Int32 traced values use callbacks with (oldValue, newValue).
         *
         * Result: whenever m_myInt changes, connected callbacks are invoked.
     * \return The TypeId.
     */
    static TypeId GetTypeId()
    {
        static TypeId tid = TypeId("MyObject")
                                .SetParent<Object>()
                                .SetGroupName("Tutorial")
                                .AddConstructor<MyObject>()
                                .AddTraceSource("MyInteger",
                                                "An integer value to trace.",
                                                MakeTraceSourceAccessor(&MyObject::m_myInt),
                                                "ns3::TracedValueCallback::Int32");
        return tid;
    }

    MyObject()
    {
    }

    // TracedValue wraps an integer and fires trace notifications on writes.
    TracedValue<int32_t> m_myInt; //!< The traced value.
};

// Trace sink: receives previous and current value from the trace source.
void
IntTrace(int32_t oldValue, int32_t newValue)
{
    std::cout << "Traced " << oldValue << " to " << newValue << std::endl;
}

int
main(int argc, char* argv[])
{
    // 1) Create an instance of MyObject (ref-counted ns-3 object).
    Ptr<MyObject> myObject = CreateObject<MyObject>();

    // 2) Connect callback IntTrace to trace source name "MyInteger".
    // WithoutContext means callback arguments contain only source-specific
    // data (no extra path/context string).
    myObject->TraceConnectWithoutContext("MyInteger", MakeCallback(&IntTrace));

    // 3) Change the traced value: this triggers IntTrace(0, 1234).
    // Since m_myInt default-initializes to 0, output shows old -> new value.
    myObject->m_myInt = 1234;

    // Overall tracing task in this code:
    // expose one integer as a trace source, subscribe to it, and confirm that
    // a state change emits a callback notification.
    return 0;
}
