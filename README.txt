NSSM: The Non-Sucking Service Manager
Version 1.0, 2003-05-30

NSSM is a service helper program similar to srvany and cygrunsrv.  It can 
start any application as an NT service and will restart the service if it 
fails for any reason.

NSSM also has a graphical service installer and remover.


Installation
------------
To install a service, run

    nssm install servicename

You will be prompted to enter the full path to the application you wish 
to run and any commandline options to pass to that application.

Use the system service manager (services.msc) to control advanced service 
properties such as startup method and desktop interaction.  NSSM may 
support these options at a later time...


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


Removing services
-----------------
NSSM can also remove services.  Run

    nssm remove servicename

to remove a service.  You will prompted for confirmation before the service 
is removed.  Try not to remove essential system services...


Licence
-------
NSSM is public domain.  You may unconditionally use it and/or its source code 
for any purpose you wish.
