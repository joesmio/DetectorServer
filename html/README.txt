///////////////////////////////////////////////////////

CQP Device Server, Early Experimental Prototype

Eric Johnston, University of Bristol Centre for Quantum Photonics
20 April 2016

These files compose a simple standalone device controller, currently
used to control the Polatis optical switch in the 2nd floor wetlab.

*** This is a prototype ***
Once we decide we like how it works, there'll be some dev work to make
things robust and future-happy.


To run:
    python pyserver.py
    Then, direct your browser to http://localhost:8080, or to see it from
    another machine, replace "localhost" with the host machine's IP address.


List of files:
    index.html - This is the control page. You can view it with or without
        a web server, but the device controls won't work without the server.

    pyserver_simple.py - This is just a simple standalone web server. Run it,
        and it will serve the pages in this directory so you can access them
        from anywhere.

    pyserver.py - This is a **MODIFIED** version of the server which also
        accepts custom commands, to allow serial device control (or whatever
        else we want). Care should be taken not to add things which give users
        general control over the local machine.

    images, icons - Images used by the web page

    manuals - User's manuals for the devices we control. It's handy to have them
        here so we can provide a nice "RTFM" link on the page.

    config/config.js - This is a JavaScript text file where we can declare any
        config variables we need, such as the names and locations of lab users.

Please direct questions and snacks to e.johnston@bristol.ac.uk

