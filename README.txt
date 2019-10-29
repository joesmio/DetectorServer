
Detector server notes
Eric Johnston, Universiy of Bristol


To build on Linux:
        sudo apt-get install libusb-1.0-0-dev
        make

To run on Linux:
        ./run_cqp_detectotron




Overall structure

Machines involved:
    DetServer - A server, physically connected to a Waterloo box.
    WebServer - A web server, which may or may not be the same machine.
                One WebServer can be used to access any number of DetServers.
    Listeners - Clients which connect to the DetServer to receive count data

Software:
    run_cqp_detectotron - The DetServer software


A note about permissions:
    We *really* needed to b able to reset the DetServer from a web button,
    but I was unable to make PHP launch the Waterloo box


Q&A:

Q. Can I view more than one histogram at the same time on the web page?
A. Only by opening moe pages. Because hitograms can be data-heavy, there's a
   limit of one per listener.




