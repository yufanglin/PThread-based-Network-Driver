# PThread-based-Network-Driver
Project for Operating Systems class

# General Overview
A piece of code is required which will function as a (de)multiplexor for a simple network device.
Calls will be made to this software component to pass packets of data onto the network. Packets
arriving from the network will be demultiplexed and passed to appropriate waiting callers. The
emphasis is on the concurrency, not on networking issues.
