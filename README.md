# Linuxify

> [!IMPORTANT]
> This project has been abandoned, due to how complicated it got.
> The shell is unmaintained, use wisely.

**Linuxify** is a shell environment and development platform for [Windows](https://en.wikipedia.org/wiki/Microsoft_Windows) that offers the familiarity of [Linux](https://en.wikipedia.org/wiki/Linux) in the command-line. **Linuxify** offers a wide range of services/features that a modern *CLI* user may need. Such as:

- **SAAO** : this is **Linuxify's** main core component and its main feature that sets it apart from every 
other shells, while every other shells. **SAAO** stands for *Session As An Object*. Which means you can create a new session and switch to it, without having to make a new tab in your terminal. It has a hierarchical model and navigation can be abit difficult. You can read all about it in: 
"SAAO\info\Sessions as an Object.md".

- **Registry** : this component basically keeps track of every package, applications and commands in the users device. So the user can dynamically use their package, app or commands in the shell, its also responsible for keeping track of the list of packages in Winget when you use **Lin**. 

- **Universal She-bang** : You can now dynamically associate a file with it's interpreter by simply doing: `#!<interpreter-path or interpreter-name>` in the very first line of a text file. As long as the interpreter is in the registry.

- **History and Auto-Suggest** : As you use **Linuxify**, it will record every input in its history file. So you can still reuse previous commands you previously used with the *Up* and *Down* arrow keys. It also uses your history in its **Auto Suggest**. As you type away, you may see **Ghost Texts**, which are grey characters that pop up after your cursor, these are non existent text that the Shell generates to help you type the rest of your input, by pressing *left* arrow key to automatically type the **Ghost Texts**

- **Custom Interpreter** : It has its own custom made interpreter in which it will run any ***.sh*** files as long as it has the `#!lish` shebang line. It is closely made to resemble [bash's](https://en.wikipedia.org/wiki/Bash) syntax, It is not yet perfect but it is usable and functional.

- **Git Awareness** : The shell is naturally *git-aware*. When traversing your file system with `cd` or its automatic cd, the prompt will dynamically change to: `linuxify:~/<path>@<branch>$`. The paths colour will change to purple signifying you are in a [git](https://en.wikipedia.org/wiki/Git) repository, it also shows which branch the repository is on, and if the branch has any uncommitted changes or not.

- **Syntax Highlighting** : The shell has a built in syntax highlighting to show if you are typing a command correctly or not, it also changes the colour of the characters when you are writing an argument for a command or a flag.

- **Auto-Navigation and Globbing** : While the `cd` command exists. The shell lets you directly switch directly by typing in the name of the directory: `linuxify:~$ Documents` or by `linuxify:~$ ./Documents`. It also supports **Globbing** `<command> <*.ext>`

- **Environmental Objects** : The shell already has environmental variables and objects. The shell also has **Environmental Objects** which holds multiple values with their corresponding keys and can even hold arrays. Allowing the user to structure and organize their data.

- **Terminal and Text Editor** : The shell also comes with its own *Terminal* and *Command-line Text Editor*: **Windux** and **Lino**. They can be used outside of **Linuxify**, the *Terminal* can be configured to use different shells.

---
## Preview

![Linuxify Preview](/assets/Prev_img)

![Linuxify Preview 2](/assets/Linuxify-Preview2.png)




---
## Installation

To install **Linuxify**, simply:

  1. Go to [*Releases*](https://github.com/patrickcortez/Linuxify/releases) and click on the latest release
  
  2. Download the installer in the *Assets* portion of the release

  3. Run the installer and install on `C:\Program Files\`


---
## Usage

After installing **Linuxify**, you can go ahead and open *Terminal* then click on the drop-down menu besides the plus sign and choose **Linuxify** or:

```cmd

linuxify

```

---
## License

**Linuxify** is under the GNU General Public License v3.0, see LICENSE for more details.
