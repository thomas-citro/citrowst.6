## citrowst.6

**Memory Management (Project 6)**
This project is an implementation of a memory management module for our Operating Systems Simulator oss. In particular, this project implements the FIFO (CLOCK) page replacement algorithm. This algorithm will select the victim frame based on our FIFO replacement policy when we need to swap an occupied frame. This treats the frames as one large circular queue. To avoid deadlocks, this project advances the clock in the main loop of oss when the users are queued for device.

This program will generate a log file called oss.log. An example outputted log file is given - it's called "logCopy".

To build this program run:
```
make
```

To run this program:
```    
./oss
```

To cleanup run:
```
make clean
```

Version Control:
https://github.com/thomas-citro/citrowst.6/
