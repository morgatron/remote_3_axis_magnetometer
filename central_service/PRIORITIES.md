# Priorities for the central service:
- Simple to understand
- simple for users to access data subsets and use with known tools (e.g. python, numpy)
- lightweight
- Support for a modest number of nodes: maybe 10, 50 at most.
- Analysis is not a priority - users can do their own analysis for now.
- Keep the nodes simple


# Network architecthure
- The central data server will likely run on a raspberry pi, in which case it should be easily installable
- The exact connectivity of each node is not entirely clear. Curently nodes will likely use Wifi or BT LE to transmit data back to a base, internet connected device (potentially another ESP32). The base is what will send the data to the central server.
    - A LoRa connected device is also possible
