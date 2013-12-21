NSSM: The Non-Sucking Service Manager
Version 2.21, 2013-11-24

NSSM is a service helper program similar to srvany and cygrunsrv.  It can 
start any application as an NT service and will restart the service if it 
fails for any reason.

NSSM also has a graphical service installer and remover.

Full documentation can be found online at

                              http://nssm.cc/

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

Since version 2.11, NSSM respects srvany's AppEnvironment parameter.

Since version 2.13, NSSM is translated into French.
Thanks François-Régis Tardy.

Since version 2.15, NSSM is translated into Italian.
Thanks Riccardo Gusmeroli.

Since version 2.17, NSSM can try to shut down console applications by
simulating a Control-C keypress.  If they have installed a handler routine
they can clean up and shut down gracefully on receipt of the event.

Since version 2.17, NSSM can redirect the managed application's I/O streams
to an arbitrary path.

Since version 2.18, NSSM can be configured to wait a user-specified amount
of time for the application to exit when shutting down.

Since version 2.19, many more service options can be configured with the
GUI installer as well as via the registry.

Since version 2.19, NSSM can add to the service's environment by setting
AppEnvironmentExtra in place of or in addition to the srvany-compatible
AppEnvironment.

Since version 2.22, NSSM can rotate existing output files when redirecting I/O.


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

If you want to include quotes in the options you will need to """quote""" the
quotes.


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


Stopping the service
--------------------
When stopping a service NSSM will attempt several different methods of killing
the monitored application, each of which can be disabled if necessary.

First NSSM will attempt to generate a Control-C event and send it to the
application's console.  Batch scripts or console applications may intercept
the event and shut themselves down gracefully.  GUI applications do not have
consoles and will not respond to this method.

Secondly NSSM will enumerate all windows created by the application and send
them a WM_CLOSE message, requesting a graceful exit.

Thirdly NSSM will enumerate all threads created by the application and send
them a WM_QUIT message, requesting a graceful exit.  Not all applications'
threads have message queues; those which do not will not respond to this
method.

Finally NSSM will call TerminateProcess() to request that the operating
system forcibly terminate the application.  TerminateProcess() cannot be
trapped or ignored, so in most circumstances the application will be killed.
However, there is no guarantee that it will have a chance to perform any
tidyup operations before it exits.

Any or all of the methods above may be disabled.  NSSM will look for the
HKLM\SYSTEM\CurrentControlSet\Services\<service>\Parameters\AppStopMethodSkip
registry value which should be of type REG_DWORD set to a bit field describing
which methods should not be applied.

  If AppStopMethodSkip includes 1, Control-C events will not be generated.
  If AppStopMethodSkip includes 2, WM_CLOSE messages will not be posted.
  If AppStopMethodSkip includes 4, WM_QUIT messages will not be posted.
  If AppStopMethodSkip includes 8, TerminateProcess() will not be called.

If, for example, you knew that an application did not respond to Control-C
events and did not have a thread message queue, you could set AppStopMethodSkip
to 5 and NSSM would not attempt to use those methods to stop the application.

Take great care when including 8 in the value of AppStopMethodSkip.  If NSSM
does not call TerminateProcess() it is possible that the application will not
exit when the service stops.

By default NSSM will allow processes 1500ms to respond to each of the methods
described above before proceeding to the next one.  The timeout can be
configured on a per-method basis by creating REG_DWORD entries in the
registry under HKLM\SYSTEM\CurrentControlSet\Services\<service>\Parameters.

  AppStopMethodConsole
  AppStopMethodWindow
  AppStopMethodThreads

Each value should be set to the number of milliseconds to wait.  Please note
that the timeout applies to each process in the application's process tree,
so the actual time to shutdown may be longer than the sum of all configured
timeouts if the application spawns multiple subprocesses.


I/O redirection
---------------
NSSM can redirect the managed application's I/O to any path capable of being
opened by CreateFile().  This enables, for example, capturing the log output
of an application which would otherwise only write to the console or accepting
input from a serial port.

NSSM will look in the registry under
HKLM\SYSTEM\CurrentControlSet\Services\<service>\Parameters for the keys
corresponding to arguments to CreateFile().  All are optional.  If no path is
given for a particular stream it will not be redirected.  If a path is given
but any of the other values are omitted they will be receive sensible defaults.

  AppStdin: Path to receive input.
  AppStdout: Path to receive output.
  AppStderr: Path to receive error output.

Parameters for CreateFile() are providing with the "AppStdinShareMode",
"AppStdinCreationDisposition" and "AppStdinFlagsAndAttributes" values (and
analogously for stdout and stderr).

In general, if you want the service to log its output, set AppStdout and
AppStderr to the same path, eg C:\Users\Public\service.log, and it should
work.  Remember, however, that the path must be accessible to the user
running the service.


File rotation
-------------
When using I/O redirection, NSSM can rotate existing output files prior to
opening stdout and/or stderr.  An existing file will be renamed with a
suffix based on the file's last write time, to millisecond precision.  For
example, the file nssm.log might be rotated to nssm-20131221T113939.457.log.

NSSM will look in the registry under
HKLM\SYSTEM\CurrentControlSet\Services\<service>\Parameters for REG_DWORD
entries which control how rotation happens.

If AppRotateFiles is missing or set to 0, rotation is disabled.  Any non-zero
value enables rotation.

If AppRotateSeconds is non-zero, a file will not be rotated if its last write
time is less than the given number of seconds in the past.

If AppRotateBytes is non-zero, a file will not be rotated if it is smaller
than the given number of bytes.  64-bit file sizes can be handled by setting
a non-zero value of AppRotateBytesHigh.

Rotation is independent of the CreateFile() parameters used to open the files.
They will be rotated regardless of whether NSSM would otherwise have appended
or replaced them.


Environment variables
---------------------
NSSM can replace or append to the managed application's environment.  Two
multi-valued string (REG_MULTI_SZ) registry values are recognised under
HKLM\SYSTEM\CurrentControlSet\Services\<service>\Parameters.

AppEnvironment defines a list of environment variables which will override
the service's environment.  AppEnvironmentExtra defines a list of
environment variables which will be added to the service's environment.

Each entry in the list should be of the form KEY=VALUE.  It is possible to
omit the VALUE but the = symbol is mandatory.

srvany only supports AppEnvironment.


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
NSSM is known to compile with Visual Studio 2008.  Older Visual Studio
releases may or may not work.

NSSM will also compile with Visual Studio 2010 but the resulting executable
will not run on versions of Windows older than XP SP2.  If you require
compatiblity with older Windows releases you should change the Platform
Toolset to v90 in the General section of the project's Configuration
Properties.


Credits
-------
Thanks to Bernard Loh for finding a bug with service recovery.
Thanks to Benjamin Mayrargue (www.softlion.com) for adding 64-bit support.
Thanks to Joel Reingold for spotting a command line truncation bug.
Thanks to Arve Knudsen for spotting that child processes of the monitored
application could be left running on service shutdown, and that a missing
registry value for AppDirectory confused NSSM.
Thanks to Peter Wagemans and Laszlo Keresztfalvi for suggesting throttling restarts.
Thanks to Eugene Lifshitz for finding an edge case in CreateProcess() and for
advising how to build messages.mc correctly in paths containing spaces.
Thanks to Rob Sharp for pointing out that NSSM did not respect the
AppEnvironment registry value used by srvany.
Thanks to Szymon Nowak for help with Windows 2000 compatibility.
Thanks to François-Régis Tardy for French translation.
Thanks to Emilio Frini for spotting that French was inadvertently set as
the default language when the user's display language was not translated.
Thanks to Riccardo Gusmeroli for Italian translation.
Thanks to Eric Cheldelin for the inspiration to generate a Control-C event
on shutdown.
Thanks to Brian Baxter for suggesting how to escape quotes from the command prompt.
Thanks to Russ Holmann for suggesting that the shutdown timeout be configurable.
Thanks to Paul Spause for spotting a bug with default registry entries.
Thanks to BUGHUNTER for spotting more GUI bugs.
Thanks to Doug Watson for suggesting file rotation.

Licence
-------
NSSM is public domain.  You may unconditionally use it and/or its source code 
for any purpose you wish.
