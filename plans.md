# Plans

Linuxify package manager:
Status: {NOT STARTED}

Name: lpm (Client Side)

Commands:

lpm update : Update the package list
lpm install <package-name> : get package from server and install on client with its dependencies.
lpm remove <package-name> : Remove a package
lpm search <package-name> : Search for a package
lpm list : List installed packages

ServerSide(Ubuntu-Server):

server.exe will always listen to a package folder


update request: update clients list of packages

install request: send package to client

search request: search for package

Packages Folder:

every package must have a folder and the server will look inside the folder and find its inf.json for its contents then ship the exe with the said contents, all contents must be in the packages folder.


Package folder Structure:

package
    |
    \sample-package
        |
        \inf.json
        \sample-package.exe
        \sample-file.dll
        \sample-file.txt
        \sample-folder
            |
            \sample-file.db

Networking:

All end users of linuxify will connect to the yggdrasil network, so it can then connect to the server, but the server will have extensive security protocols to prevent unauthorized access and only forward what ever requests each end user will have.


---

client -> server
server -> client


---

Data Grid System:
Status: {DONE}

Name: Linuxify Grid System (LGS)

Purpose: A user defined data grid that can be configured and used by the user similar to sql, by piping the data to different processes.

commands:

grid create <grid-name> : create a new grid(binary file)
grid use <grid-name> : use a grid
grid add <column-name>:<data-type> : add a column to the grid
grid insert <row-data> : insert data to the grid
grid select <condition>: select data from the grid
grid edit <row-index> <column-index> <data>: edit data in the grid
grid delete-row <row-index> : delete a row from the grid
grid delete-column <column-index> : delete a column from the grid
grid delete <row-index> <column-index> : delete a singular data from the grid
grid list : list all grids
grid list -c : list all columns in the grid
grid list -r : list all rows in the grid
grid list -a : list all datas in grid row+column
grid import : for piping other command output to grid: ps | grid import

Note:
No interactive shell like sql so data can be piped to different processes.
We store binary grid files in a folder called "grids".

Data types:
 - String
 - Integer
 - Float
 - Boolean


-----------------------------
| "Name" | "Age" | "Trusted"| 00,01,02 <- Position
----------------------------- 10,11,12 <- Position
| "John" |  20   | True     | 20,21,22 <- Position
-----------------------------
| "Jane" |  22   | False    |
-----------------------------