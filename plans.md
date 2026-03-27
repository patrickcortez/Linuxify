# Plans

Linuxify package manager:
Status: {NOT STARTED}

Name: lpm (Client Side)(C++)

Commands:

lpm update : Update the package list
lpm install <package-name> : get package from server and install on client.
lpm remove <package-name> : Remove a package
lpm search <package-name> : Search for a package
lpm list : List installed packages

ServerSide(Ubuntu-Server)(python):

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


The Server side will compile and compress the entire package folder into a .lpm binary executable file, which will be handled by the client side, and successfully install the said package.

Networking:

All end users of linuxify will connect to the yggdrasil network, so it can then connect to the server, but the server will have extensive security protocols to prevent unauthorized access and only forward what ever requests each end user will have.

Security:

Security is going to be a classic public and private key, where each client will have the servers public key, and upon any request, client will send their public key with the request. The server will then confirm if the said client is legit or not, and if legit, it will send the package to the client.

So the key will be made with lpm key-gen, which will generate a public and private key for the client. the said keys will be stored in the clients home directory in a `.lpm` folder, inside are the files: <filename>.pub and <filename>.pri

---

client -> server
server -> client

---

Environmental Objects:

An environmental object is a variable that can hold multiple values with their corresponding keys containing the said values. These values can be a string literal or a integer.

export -obj <Object-name>{<key>:<value:string-or-int>,...} //Basically this is how a user can declare an environmental object and it also can be persistent like arrays and variables, and yes objects can have arrays inside them.

to use:

<command> $<Obj-name>.<member-name>

Example:

//without arrays:

export -obj user{name:"Cortez",age:20,gender:"Male"}

echo "$user.name" //Output: Cortez

echo "$user.age" //Output: 20

echo "$user.gender" //Output: Male

//with arrays:

export -obj user{name:"Cortez",age:20,gender:"Male",hobbies:["Gaming","Coding","Reading"]}

echo "$user.hobbies[0]" //Output: Gaming

echo "$user.hobbies[1]" //Output: Coding

echo "$user.hobbies[2]" //Output: Reading

//persistent objects:

export -obj -p user{name:"Cortez",age:20,gender:"Male",hobbies:["Gaming","Coding","Reading"]}

//to remove persistent objects:

var del <obj-name>

---



