# CSIEBOX
It is a DropBox-like program. It can help user synchronize two directory.
#### I only contribute to ``csiebox_server.c`` ``csiebox_client.c``, other codes provide by the TA of the System Programming Class
## Usage
### STEP 1 - Compile
Under ``src/`` directory
```
make
```
### STEP 2 - Configuration under ``config/` directoty
1. server.cfg Path: ``../sdir`` 
Account_path: ``../config/account``

2. client.cfg 
Name: ``<your account name, student ID in work station>``
Server: ``localhost``
User: ``<username in account file>`` 
Passwd: ``<password in account file>`` 
Path: ``../cdir``

3. account 
``<username>,<password>``
### STEP 3 - Run port register under ``bin/`` directory
```
./port_register
```
### STEP 3 - Start up server under ``bin/`` directory
```
./csiebox_server ../config/server.cfg
```
### STEP 4 - Start up client under ``bin/`` directory
```
./csiebox_client ../config/client.cfg
```
Now you can see every change in cdir would synchronize under ``sdir/[account name]``
## Deficiency
* It would not handle file created/edited by vim.
* It would not handle symbolic link.
##More detailed SPEC
```
https://systemprogrammingatntu.github.io/MP2
```
