
# CDAQ JANA + TCP

The cdaq plugin supports both reading and writing CDAQ events over TCP. It can do this simultaneously so that any number of JANA processes can be linked together into a pipeline. 

Below is an example that connects 3 processes where one process reads events from a file and sends them to another process via TCP. The second process then sends the events on to the 3rd process via TCP. The processes should generally be started in the order starting with the last process in the chain and going towards the first (i.e. the process reading events from the file or live data stream should be started last.)

~~~
> jana4ml4fpga -Pplugins=cdaq -Pcdaq:port=20250 tcpcdaqevio
> jana4ml4fpga -Pplugins=cdaq -Pcdaq:port=20249 -Pcdaq:remote_port=20250 tcpcdaqevio
> jana4ml4fpga -Pplugins=CDAQfile,cdaq DATA/hd_rawdata_003101_000.evio -Pcdaq:remote_port=20249
~~~

In the above commands, the `cdaq:port` configuration parameter is used to specify the port the process will listen on to receive events. The `cdaq:remote_port` is used to specify the port to connect to and send events over. There are corresponding `cdaq:host` and `cdaq:remote_host` variables that can be used to specify alternative hosts. If a host name is not specified, it defaults to `localhost`.

Note that processes receiving events via TCP should specify the event source with the special string `tcpcdaqevio`. 


## Limitations

This works by sending the *unparsed* data block read from either the input file or the TCP socket over the network. This means that it sends out *__exactly__* the data that was read in. Each `JANA` process can parse the data and run factories to produce new data objects, but those will not be sent over the TCP connection.

<hr>

## Demonstration

The screen recording below shows three processes communicating CDAQ events via TCP. This was done using the `hdtrdops` account on `gluon200` while in the `~/GemTrd_2023` directory.


![Demo showing 3 jana4ml4fpga processes passing CDAQ events between them](../../../doc/CDAQTCP_example.gif?raw=true "cdaq Demo")
