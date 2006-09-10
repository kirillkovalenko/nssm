NSSM: The Non-Sucking Service Manager
Version 2.0, 2006-09-09

NSSM is a service helper program similar to srvany and cygrunsrv.  It can 
start any application as an NT service and will restart the service if it 
fails for any reason.

NSSM also has a graphical service installer and remover.

Since version 2.0, the GUI can be bypassed by entering all appropriate 
options on the command line.


Usage
-----
In the usage notes below, arguments to the program may be written in angle 
brackets and/or square brackets.  <string> means you must insert the 
appropriate string and [<string>] means the string is optional.  See the 
examples below...


Installation using the GUI
--------------------------
To install a service, run

    nssm install <servicename>

You will be prompted to enter the full path to the application you wish 
to run and any command line options to pass to that application.

Use the system service manager (services.msc) to control advanced service 
properties such as startup method and desktop interaction.  NSSM may 
support these options at a later time...


Installation using the command line
-----------------------------------
To install a service, run

    nssm install <servicename> <application> [<options>]

NSSM will then attempt to install a service which runs the named application 
with the given options (if you specified any).

Don't forget to enclose paths in "quotes" if they contain spaces!


Managing the service
--------------------
NSSM will launch the application listed in the registry when you send it a 
start signal and will terminate it when you send a stop signal.  So far, so 
much like srvany.  But NSSM is the Non-Sucking service manager and will take 
action if/when the application dies.

NSSM will try to restart itself if it notices that the application died but 
you didn't send it a stop signal.  NSSM will keep trying, pausing 30 seconds 
between each attempt, until the service is successfully started or you send 
it a stop signal.


Removing services using the GUI
-------------------------------
NSSM can also remove services.  Run

    nssm remove <servicename>

to remove a service.  You will prompted for confirmation before the service 
is removed.  Try not to remove essential system services...


Removing service using the command line
---------------------------------------
To remove a service without confirmation from the GUI, run

    nssm remove <servicename> confirm

Try not to remove essential system services...


Example usage
-------------
To install an Unreal Tournament server:

    nssm install UT2004 c:\games\ut2004\system\ucc.exe server

To remove the server:

    nssm remove UT2004 confirm


Building NSSM from source
-------------------------
NSSM is known to compile with Visual Studio 6 and Visual Studio 2005.


Licence
-------
NSSM is public domain.  You may unconditionally use it and/or its source code 
for any purpose you wish.
