# Sessions as an Object

So this is **S.A.A.O** or *Session As An Object*, this basically treats each sessions you make, delete and switch to as an object, and its command syntax is also object like with the `<command>:<sub>[args]`.
**SAAO** is quite similar to screen but its more hierarchical and each session is basically a state-machine where their states are: *Active*, *Background* or *Terminated*. **SAAO** is highly useful in Dev-ops, system administration and hopefully for enterprises. If you want  multiple sessions without switching tabs or having too many tabs in your terminal. Then **SAAO** is the way to go!

Now **SAAO** will have something like:

```Linuxify

linuxify[session-no]:~/$ <current-session-number/name>:<subcommand>[arguments]

```

So What I have  though is this, session management in one tab, you can dynamically switch sessions, which should be helpful, since switching sessions would legit preserve the environment of a sessions and its processes. Basically the session not used becomes a background process. For convenience we will add an *indicator* besides the shell name on what session number the user is currently in

The user also has to know the current session name or number, because we will return an *error* once they get it wrong. which should be easy since we have an *indicator*.

So the basics are is that, the first session the user starts with when they open the terminal is the **master session** or session *0*, yes the session will be numerical by default, but sessions can be named, depending on the users preferences. adding a new session would be simple:

```Linuxify

linuxify[0]:~/$ <0>:<add>[(options: no flag=numeric, -n <session name>)]

linuxify[0]:~/$ <0>:<add>[] //Note: this is a numeric based session name

linuxify[0]:~/$ <0>:<add>[-n newSesh] //Note: this is a name based session name

```

so that with that the user just added a new session, that new session is active and runs as a background process, the user must use the *switch* subcommand to switch to the new session.
It is important to note that the new session is a child of the **master session**. Any session created in a session, the new session is a child of that session. We should also note that even in named sessions that session still has a session number, its session number is the session number of its left sibling + 1, 

How session numbering works is simple, it starts with our **master session** which is 0, then a new child session is made, which takes a +1 from its parent, the next child will take a +1 from its left most sibling which is its latest sibling. So if the *Master* is 0 and the new child is 1, then the next child is 2, which is the session number of *newSesh*.

So now that we covered **session creation**, we will now tackle **session switching**. Which is essential, as you the user, cannot use the new session you just made without it, to switch is simple:

```Linuxify

linuxify[0]:~/$ <0>:<switch>[(session-number/name)]

linuxify[0]:~/$ <0>:<switch>[1] //Note: this is a numerical session switch

linuxify[0]:~/$ <0>:<switch>[newSesh] //this is a named session switch

```

Switching sessions is relatively simple, its just like switching branches in *git*. You either do a *named switch* or a *numeric switch*. However you must take in mind that you cannot switch to a session that is not a direct child of that session, You can only switch session to a parent session or a child session, not grandchild session or sibling session.

Now that we covered **Switching**. We will cover the last which is *Session Popping/Deleting*, Now Session Popping is simple, if you dont want a session you can simply delete it, but of course you cannot delete the session you are currently in, you must always switch to a *Parent Session*. not a *child*, as if you delete a *Session* that has children, those children will be inaccessible, and will be referred to as *Lost Sessions*. Lost sessions will be terminated, and Also you cannot erase the master session:

```Linuxify

linuxify[1]:~/$ <1>:<switch>[0]

linuxify[0]:~/$ <0>:<pop>[1] //Note: now after this 1 is now gone and newSesh is the only child remaining

```

Now that we covered all of the basics in **Session Management**, you should be able to perform relatively well in this new upcoming feature!


# Architecture

The architecture is simple and its strictly Hierarchal by nature, since its a parent and child model.
In which the parent session would have some control over the child session, in future versions, I will implement some form of control from parent sessions, so the user can automate tasks without switching to that session. 


For a visual aid at what it looks like, see [[SAAO Hierarchy.canvas]].

Developer: *Cortez*