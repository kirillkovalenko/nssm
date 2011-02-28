NSSM: The Non-Sucking Service Manager
Version 2.9, 2011-02-28

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

Since version 2.3, NSSM logs to the Windows event log more elegantly.

Since version 2.5, NSSM respects environment variables in its parameters.

Since version 2.8, NSSM tries harder to shut down the managed application
gracefully and throttles restart attempts if the application doesn't run
for a minimum amount of time.

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
keep trying, pausing between each attempt, until the service is successfully
started or you send it a stop signal.

NSSM will pause an increasingly longer time between subsequent restart attempts
if the service fails to start in a timely manner, up to a maximum of four
minutes.  This is so it does not consume an excessive amount of CPU time trying
to start a failed application over and over again.  If you identify the cause
of the failure and don't want to wait you can use the Windows service console
(where the service will be shown in Paused state) to send a continue signal to
NSSM and it will retry within a few seconds.

By default, NSSM defines "a timely manner" to be within 1500 milliseconds.
You can change the threshold for the service by setting the number of
milliseconds as a REG_DWORD value in the registry at
HKLM\SYSTEM\CurrentControlSet\Services\<service>\Parameters\AppThrottle.

NSSM will look in the registry under
HKLM\SYSTEM\CurrentControlSet\Services\<service>\Parameters\AppExit for
string (REG_EXPAND_SZ) values corresponding to the exit code of the application.
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

If the value data is "Exit" NSSM will exit gracefully.  The Windows Services
console would show the service as stopped.  If you wish to provide
finer-grained control over service recovery you should use this code and
edit the failure action manually.  Please note that Windows versions prior
to Vista will not consider such an exit to be a failure.  On older versions
of Windows you should use "Suicide" instead.

If the value data is "Suicide" NSSM will simulate a crash and exit without
informing the service manager.  This option should only be used for
pre-Vista systems where you wish to apply a service recovery action.  Note
that if the monitored application exits with code 0, NSSM will only honour a
request to suicide if you explicitly configure a registry key for exit code 0.
If only the default action is set to Suicide NSSM will instead exit gracefully.


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


Logging
-------
NSSM logs to the Windows event log.  It registers itself as an event log source
and uses unique event IDs for each type of message it logs.  New versions may
add event types but existing event IDs will never be changed.

Because of the way NSSM registers itself you should be aware that you may not
be able to replace the NSSM binary if you have the event viewer open and that
running multiple instances of NSSM from different locations may be confusing if
they are not all the same version.


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
Thanks to Bernard Loh for finding a bug with service recovery.
Thanks to Benjamin Mayrargue (www.softlion.com) for adding 64-bit support.
Thanks to Joel Reingold for spotting a command line truncation bug.
Thanks to Arve Knudsen for spotting that child processes of the monitored
application could be left running on service shutdown, and that a missing
registry value for AppDirectory confused NSSM.
Thanks to Peter Wagemans and Laszlo Keresztfalvi for suggesting throttling restarts.

Licence
-------
NSSM is public domain.  You may unconditionally use it and/or its source code 
for any purpose you wish.
