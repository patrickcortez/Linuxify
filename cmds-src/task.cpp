#include <iostream>
#include <vector>

/*
 Commands:
    list
    add
    check
    remove
*/

namespace Task{

    struct obj{
        int id;
        std::string goal;
        bool done;
    };


    class List{
        private:
            std::vector<obj> tasks; //init list of tasks

        public:

            void add(const obj& newobj = nullptr){

                if(newobj == nullptr){
                    return;
                }else{
                    tasks.push_back(newobj);
                }
            }

            void remove(int id){

            }

    };

}



int main() {
    std::cout << "Hello, World!" << std::endl;
    return 0;
}