NSSM: The Non-Sucking Service Manager
Version 2.2, 2010-04-04

NSSM is a service helper program similar to srvany and cygrunsrv.  It can 
start any application as an NT service and will restart the service if it 
fails for any reason.

NSSM also has a graphical service installer and remover.

Full documentation can be found online at

                           http://iain.cx/src/nssm/

Since version 2.0, the GUI can be bypassed by entering all appropriate 
options on the command line.

Since version 2.1, NSSM can be compiled for x64 platforms.
Thanks Benjamin Mayrargue.

Since version 2.2, NSSM can be configured to take different actions
based on the exit code of the managed application.


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
much like srvany.  But NSSM is the Non-Sucking service manager and can take 
action if/when the application dies.

With no configuration from you, NSSM will try to restart itself if it notices
that the application died but you didn't send it a stop signal.  NSSM will
keep trying, pausing 30 seconds between each attempt, until the service is
successfully started or you send it a stop signal.

NSSM will look in the registry under
HKLM\SYSTEM\CurrentControlSet\Services\<service>\Parameters\AppExit for
string (REG_SZ) values corresponding to the exit code of the application.
If the application exited with code 1, for instance, NSSM will look for a
string value under AppExit called "1" or, if it does not find it, will
fall back to the AppExit (Default) value.  You can find out the exit code
for the application by consulting the system event log.  NSSM will log the
exit code when the application exits.

Based on the data found in the registry, NSSM will take one of three actions:

If the value data is "Restart" NSSM will try to restart the application as
described above.  This is its default behaviour.

If the value data is "Ignore" NSSM will not try to restart the application
but will continue running itself.  This emulates the (usually undesirable)
behaviour of srvany.  The Windows Services console would show the service
as still running even though the application has exited.

If the value data is "Exit" NSSM will exit.  The Windows Services console
would show the service as stopped.  If you wish to provide finer-grained
control over service recovery you should use this code and edit the failure
action manually.


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
NSSM is known to compile with Visual Studio 6, Visual Studio 2005 and Visual
Studio 2008.


Credits
-------
Thanks to Benjamin Mayrargue (www.softlion.com) for adding 64-bit support.

Licence
-------
NSSM is public domain.  You may unconditionally use it and/or its source code 
for any purpose you wish.
