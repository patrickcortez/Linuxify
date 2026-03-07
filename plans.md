# Plans

Linuxify package manager: {Optional}

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

every package must have a folder and the server will look inside the folder and find its inf.json for its dependencies then ship the exe with the said dependencies, all dependencies must be in the packages folder.


---

client -> server
server -> client


Linuxify Status {DONE}