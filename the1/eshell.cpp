#include<parser.h>
#include<string>
#include<iostream>
#include<unistd.h>
#include<sys/wait.h>
#include<exception>
#include<vector>

class MyException : public std::exception
{
    private:
        std::string message;
    public:
        const char *what(){
            return message.c_str();
        }
        MyException(const std::string err) : std::exception() {
            message = err;
        }       
};


int myWaitPid(int pid){
    int status;
    waitpid(pid,&status,0);
    return status;
}
class LineExecuterWithoutSubshell{
    protected:        
        void executeCommand(single_input singleInput){
            executeCommandVAR(singleInput.data.cmd);
        }
        virtual void executeSubshell(single_input &input){
            throw MyException("UNEXPECTED BEHAVIOR");
        }
        virtual void executeSingleInput(single_input singleInput){
            if(singleInput.type==INPUT_TYPE_COMMAND){
                executeCommand(singleInput);
            }else if(singleInput.type==INPUT_TYPE_PIPELINE){
                executePipesVAR(singleInput);//TODO:validate
            }else if(singleInput.type==INPUT_TYPE_SUBSHELL){
                throw MyException("UNEXPECTED BEHAVIOR");
                //executeSubshell(singleInput);
            }else{
                throw MyException("UNEXPECTED BEHAVIOR");
            }
        }
        void executeCommandVAR(command &command){
            char **args = command.args;
            /*
            for(char **myargs = args; *myargs!= nullptr; myargs+=1){
                std::cout << *myargs <<"-";
            }
            std::cout<<std::endl;
            return;
            */
            if(!strcmp(args[0],"quit")){
                exit(0);
            }
            pid_t pid;
            if(pid = fork()){
                if(pid < 0){
                    throw MyException("FORK FAILED");
                }
                int status;
                waitpid(pid,&status,0);
            }else{
                execvp(args[0],args);
                throw MyException("EXEC CALL FAILED");
            }
        }
        void executePipesVAR(single_input &input){
            int n = input.data.pline.num_commands;
            int pid ;
            std::vector<int> pids ; 
            
            int prev_fd[2];
            int fd[2];
            for(int i = 0 ; i < n ; i++ ){
                
                prev_fd[0]=fd[0];
                prev_fd[1]=fd[1];
                if(i!=n-1){
                    pipe(fd);
                }
                if(pid=fork()){
                    if(i!=n-1){
                        close(fd[1]);
                    }
                    if(i!=0){
                        close(prev_fd[0]);
                    }
                    pids.push_back(pid);
                }else{
                    if(i!=0){
                        dup2(prev_fd[0],0);
                        close(prev_fd[0]);
                    }
                    if(i!=n-1){
                        dup2(fd[1],1);
                        close(fd[1]);
                        close(fd[0]);
                    }    
                    executeCommandVAR(input.data.pline.commands[i]);
                    exit(0);
                }       
            }
            
            //only the master process comes here
            for(int i = 0 ; i < n ; i++){
                myWaitPid(pids.data()[i]);
            }

        }
        void executePipes(parsed_input &input){
            int n = input.num_inputs;
            int pid ;
            std::vector<int> pids ; 
            
            int prev_fd[2];
            int fd[2];
            for(int i = 0 ; i < n ; i++ ){
                
                prev_fd[0]=fd[0];
                prev_fd[1]=fd[1];
                if(i!=n-1){
                    pipe(fd);
                }
                if(pid=fork()){
                    if(i!=n-1){
                        close(fd[1]);
                    }
                    if(i!=0){
                        close(prev_fd[0]);
                    }
                    pids.push_back(pid);
                }else{
                    if(i!=0){
                        dup2(prev_fd[0],0);
                        close(prev_fd[0]);
                    }
                    if(i!=n-1){
                        dup2(fd[1],1);
                        close(fd[1]);
                        close(fd[0]);
                    }    
                    executeSingleInput(input.inputs[i]);
                    exit(0);
                }       
            }
            
            //only the master process comes here
            for(int i = 0 ; i < n ; i++){
                myWaitPid(pids.data()[i]);
            }

        }
        virtual void executeParalel(parsed_input &input){
            std::vector<int> pids ;   
            for(int i = 0 ; i < input.num_inputs ; i++){
                int pid ;
                if(pid=fork()){
                    pids.push_back(pid);
                }else{
                    executeSingleInput(input.inputs[i]);
                    exit(0);
                }
            }
            //only the master process comes here
            for(int i = 0 ; i < input.num_inputs ; i++){
                myWaitPid(pids.data()[i]);
            }
        }
        void executeSequential(parsed_input &input){
            for(int i = 0 ; i < input.num_inputs ; i++){
                executeSingleInput(input.inputs[i]);
            }
        }
    public:
        void executeInput(char *input){
            parsed_input parsedInput;
            if(!parse_line(input,&parsedInput)){
                throw MyException("PARSE FAILED");
            }
            
                     
            if(parsedInput.separator==SEPARATOR_NONE){
                executeSingleInput(parsedInput.inputs[0]);
            }else if(parsedInput.separator==SEPARATOR_PIPE){
                executePipes(parsedInput);
            }else if(parsedInput.separator==SEPARATOR_SEQ){
                executeSequential(parsedInput);
            }else if(parsedInput.separator==SEPARATOR_PARA){
                executeParalel(parsedInput);
            }else{//should have never come here
                throw MyException("UNEXPECTED BEHAVIOR");
            }
            std::cout.flush();
        }
};
class SubshellExecuter : public LineExecuterWithoutSubshell{
    protected:
    virtual void executeParalel(parsed_input &input) override{
        std::string readString = "";
        char c;
        while(read(0,&c,1)){
            readString += c ;
        }
            
        //std::cout << std::endl << "!!!" << readString <<"!!!"<<std::endl; TODO : check ALWAYS
        std::cout.flush();
        
        int n = input.num_inputs;
        std::vector<int> fds ; 
        std::vector<int> pids ; 
        for(int i = 0 ; i < n ; i++){
            int fd[2];
            int pid;
            pipe(fd);
            if(pid = fork()){
                close(fd[0]);
                pids.push_back(pid);
                fds.push_back(fd[1]);
            }else{
                for(int j = 0 ; j < fds.size(); j++ ){
                    close(fds.data()[j]);
                }
                close(fd[1]);
                dup2(fd[0],0);
                close(fd[0]);
                executeSingleInput(input.inputs[i]);
                exit(0);
            }        
        }
        std::vector<int> repeaterPids;
        for(int i = 0 ; i < pids.size(); i++){
            int pid ;
            if(pid = fork()){
                repeaterPids.push_back(pid);
            }else{
                dup2(fds[i],1);
                for(int j = 0 ; j < fds.size(); j++){
                    close(fds[j]);
                }
                printf("%s",readString.data());
                //close(1);
                exit(0);
            }
        }
        for(int i = 0 ; i < fds.size(); i++){
            close(fds[i]);
        }
        for(int i = 0 ; i < repeaterPids.size(); i++){
            myWaitPid(repeaterPids[i]);
        }

        for(int i = 0 ; i < pids.size(); i++){
            myWaitPid(pids[i]);
        }

  
    }
};

class LineExecuter : public LineExecuterWithoutSubshell{
    protected:
        SubshellExecuter subshellExecuter;
        virtual void executeSubshell(single_input &input) override{
            subshellExecuter.executeInput(input.data.subshell);
        }
        virtual void executeSingleInput (single_input singleInput) override{
            if(singleInput.type==INPUT_TYPE_COMMAND){
                executeCommand(singleInput);
            }else if(singleInput.type==INPUT_TYPE_PIPELINE){
                executePipesVAR(singleInput);//TODO:validate
            }else if(singleInput.type==INPUT_TYPE_SUBSHELL){
                executeSubshell(singleInput);
            }else{
                throw MyException("UNEXPECTED BEHAVIOR");
            }
        }
    public:
        LineExecuter(){
            subshellExecuter = SubshellExecuter();
        }
};


void program(){
    std::string lineAsStringObject ;
    LineExecuter lineExecuter = LineExecuter();
    
    while (std::getline(std::cin, lineAsStringObject)) {    
        std::cout << "/> ";
        std::cout.flush();
        char *line = strdup(lineAsStringObject.c_str());
        lineExecuter.executeInput(line);
        free(line);
    }
}

int main() {
    try
    {   
        program();
    }
    catch(MyException ex)
    {
        std::cout <<"ERROR MESSAGE:'"<<ex.what()<<"'\n";
    }        
    return 0;
}
