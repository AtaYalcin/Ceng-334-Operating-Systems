//
// Created by ata on 01.05.2024.
//
#include <float.h>
#include <helper.h>
#include <monitor.h>
#include <iostream>
#include <vector>
#include <WriteOutput.h>
#include <pthread.h>
#include <queue>

using namespace std;
enum ConnectorType{
    FERRY,
    NARROWBRIDGE,
    CROSSROAD
};
int threadCount;

char connectorChars[3]={'F','N','C'};
class Connector: public monitor::Monitor{
protected:
    int travelTime;
    int maximumWaitTime;
    int id;

    int timedWaitHelper(Condition& cv,long time_ms) {//TODO:DEBUG HERE WELL


        struct timespec* ts = new timespec();
        clock_gettime(CLOCK_REALTIME,ts);

        long long nsec = ts->tv_nsec + 1000000*time_ms;
        ts->tv_sec += nsec / 1000000000L;
        nsec = nsec % 1000000000L;
        ts->tv_nsec = nsec;

        int wait_result = cv.timedwait(ts);

        delete ts;
        return wait_result;
    }
public:
    Connector(int idParameter,int travelTimeParameter,int maximumWaitTimeParameter):monitor::Monitor(){
        travelTime=travelTimeParameter;
        maximumWaitTime=maximumWaitTimeParameter;
        id = idParameter;
    }
    int getId() {
        return id;
    }
    virtual void pass (int from, int to, int carId) {
        cout << "NAIVE PASS CALL";
    };
};
class PathElement{
public:
    Connector *connector;
    int from;
    int to;
    ConnectorType type;
    PathElement(Connector *connectorParameter,ConnectorType typeParameter,int fromParameter, int toParameter){
        connector = connectorParameter;
        from = fromParameter;
        to = toParameter;
        type = typeParameter;
    }
};





class Ferry :public Connector{
private:
    int capacity;
    int loads[2];

    Condition cv0;
    Condition cv1;
    Condition *cvs[2];
    void passHelper0(int to) {
        __synchronized__;
        loads[to] +=1;
        if(loads[to]==capacity) {
            loads[to] = 0 ;
            cvs[to]->notifyAll();
        }else {
            if(timedWaitHelper(*cvs[to],maximumWaitTime)) {
                loads[to] = 0;
                cvs[to]->notifyAll();
            }
        }
    }
public:
    Ferry(int idParameter,int travelTimeParameter,int maximumWaitTimeParameter,int capacityParameter):Connector(idParameter,travelTimeParameter,maximumWaitTimeParameter),cv0(this),cv1(this){
        capacity = capacityParameter;
        loads[0] = 0;
        loads[1] = 0;
        cvs[0] = &cv0;
        cvs[1] = &cv1;
    }
    virtual void pass (int from, int to, int carId) override {
        passHelper0(to);
        WriteOutput(carId,'F',id,START_PASSING);
        sleep_milli(travelTime);
        WriteOutput(carId,'F',id,FINISH_PASSING);
    }

};
class NarrowBridge : public Connector{
    private:
    Condition **cvs;
    Condition noNotify;
    Condition activityChange;
    queue<int> queues[2];
    queue<int> bridge;
    int activeSide = -1 ;

    int otherSide(int thisSide){
        return 1-thisSide;
    }
    void changeActiveSide(int side) {
        //cout << endl<<"active side changed to " << side << endl ;
        activeSide = side;
        activityChange.notify();
    }
    void passHelper0(int side, int carId) {
        __synchronized__
        queues[side].push(carId);

        do {
            if(queues[side].front() == carId) {
                if(bridge.empty() && queues[otherSide(side)].empty()) {
                    changeActiveSide(side);
                }
                if(activeSide == otherSide(side)) {
                    timedWaitHelper(*cvs[carId],maximumWaitTime);
                    activeSide = side;
                    if(!bridge.empty()) {
                        cvs[carId]->wait();
                    }
                }
            }else {
                cvs[carId]->wait();
            }

            if(!bridge.empty() && activeSide ==side) {
                timedWaitHelper(activityChange,PASS_DELAY);
            }
        }while (activeSide == otherSide(side));

        queues[side].pop();
        bridge.push(carId);
        WriteOutput(carId,connectorChars[1],id,START_PASSING);

        if(!queues[side].empty()) {
            cvs[queues[side].front()]->notify();
        }

        timedWaitHelper(noNotify,travelTime);
        WriteOutput(carId,connectorChars[1],id,FINISH_PASSING);
        bridge.pop();


        if(bridge.empty()) {
            if(queues[side].empty()) {
                changeActiveSide(otherSide(side));
            }

            if(side != activeSide) {
                if(!queues[activeSide].empty()) {
                    cvs[queues[activeSide].front()]->notify();
                }
            }

        }

    }
    public:
    NarrowBridge(int idParameter,int travelTimeParameter,int maximumWaitTimeParameter):Connector(idParameter,travelTimeParameter,maximumWaitTimeParameter), noNotify(this),activityChange(this){
    }
    void secondaryInitialization() {
        cvs = new Condition*[threadCount] ;
        for(int i = 0 ; i < threadCount ; i++) {
            cvs[i] = new Condition(this);
        }
    }
    ~NarrowBridge() {

    }
    virtual void pass (int from, int to, int carId) override {
        passHelper0(from,carId);
    }
};
class CrossRoad: public Connector {
private:
    Condition **cvs;
    Condition noNotify;
    Condition activityChanged;
    queue<int> queues[4];
    queue<int> bridges[4];
    int activeSide = 0 ;

    void giveWay() {

        for(int i = 0 ; i < 4 ; i++) {
            activeSide = (activeSide+1)%4;
            if(!queues[activeSide].empty()) {
                activityChanged.notify();
                break;
            }
        }
        for(int i = 0 ; i < 4 ; i++) {
            if(!queues[i].empty()) {
                cvs[queues[i].front()]->notify();
            }
        }
    }

    void passHelper0(int from,int to, int carId) {
        __synchronized__
        queues[from].push(carId);

        do {
            if(queues[from].front() == carId) {
                if(queues[activeSide].empty() && bridges[activeSide].empty()) {
                    giveWay();
                }
                while(activeSide != from) {
                    if(timedWaitHelper(*cvs[carId],maximumWaitTime)) {
                        giveWay();
                    }
                }
            }else {
                cvs[carId]->wait();
            }
            //start optional


            bool bridgesTotallyEmpty = true;
            for(int i = 0 ; i < 4 ; i++) {
                if(!bridges[i].empty()) {
                    bridgesTotallyEmpty = false;
                    break;
                }
            }
            if(!bridgesTotallyEmpty && activeSide == from && queues[from].front()==carId) {
                if(!timedWaitHelper(activityChanged,PASS_DELAY)) {

                }
            }

        }while (activeSide != from && queues[from].front()==carId);

        queues[from].pop();
        bridges[from].push(carId);
        WriteOutput(carId,connectorChars[2],id,START_PASSING);

        if(!queues[from].empty()) {
            cvs[queues[from].front()]->notify();
        }

        timedWaitHelper(noNotify,travelTime);
        WriteOutput(carId,connectorChars[2],id,FINISH_PASSING);




        bridges[from].pop();



        if(activeSide == from) {
            if(bridges[from].empty()) {
                if(queues[from].empty()) {
                    giveWay();
                }
            }
        }

    }

    public:
    CrossRoad(int idParameter,int travelTimeParameter,int maximumWaitTimeParameter):Connector(idParameter,travelTimeParameter,maximumWaitTimeParameter),noNotify(this),activityChanged(this){}
    virtual void pass (int from, int to, int carId) override {
        passHelper0(from,to,carId);
    }
    void secondaryInitialization() {
        cvs = new Condition*[threadCount] ;
        for(int i = 0 ; i < threadCount ; i++) {
            cvs[i] = new Condition(this);
        }
    }
};

vector<NarrowBridge*> narrowBridges;
vector<Ferry*> ferries;
vector<CrossRoad*> crossRoads;

class Car{
private:
    vector<PathElement> path;
    int interConnectorTravelTime;
    int id;
public:
    Car(int idParameter,int interConnectorTravelTimeParameter){
        id = idParameter;
        path = vector<PathElement>();
        interConnectorTravelTime = interConnectorTravelTimeParameter;
    };

    void takePath() {

        for(int i = 0 ; i < path.size();i++) {

            PathElement currentElement = path[i];
            WriteOutput(id,connectorChars[currentElement.type],currentElement.connector->getId(),TRAVEL);//travel
            sleep_milli(interConnectorTravelTime);
            WriteOutput(id,connectorChars[currentElement.type],currentElement.connector->getId(),ARRIVE);//arrive
            currentElement.connector->pass(currentElement.from,currentElement.to,id);

        }

    }
    void appendToPath(int index,ConnectorType type,int from,int to){
        Connector *connecter;

        switch(type){
            case ConnectorType::FERRY:
                connecter =(Connector*) ferries[index];
            break;
            case ConnectorType::NARROWBRIDGE:
                connecter =(Connector*) narrowBridges[index];
            break;
            case ConnectorType::CROSSROAD:
                connecter =(Connector*) crossRoads[index];
            break;
            default:
                throw "ERROR";
            break;
        }

        path.push_back(PathElement(connecter,type,from,to));
    }
};

vector<Car*> cars;

int parse(){
    for(int i = 0 ; i < 3 ; i++){
        int count;
        cin >> count;
        for(int j = 0 ;  j < count ; j++){
            int travelTimeParameter;
            int maximumWaitTimeParameter;
            cin >> travelTimeParameter;
            cin >> maximumWaitTimeParameter;
            switch(i){
                case 0 :
                    narrowBridges.push_back(new NarrowBridge(j,travelTimeParameter,maximumWaitTimeParameter));
                    break;
                case 1 :
                    int capacityParameter;
                    cin >> capacityParameter;
                    ferries.push_back(new Ferry(j,travelTimeParameter,maximumWaitTimeParameter,capacityParameter));
                    break;
                case 2 :
                    crossRoads.push_back(new CrossRoad(j,travelTimeParameter,maximumWaitTimeParameter));
                    break;
                default:
                    return 1;
                    break;
            }
        }
    }
    //we have parsed all connectors.
    //now we need to parse the cars.
    //connector vectors have the ids already ordered.

    int carCount;
    cin >> carCount;
    for(int i = 0 ; i < carCount ; i++){
        int travelTimeParameter;
        int pathLengthParameter ;
        cin >> travelTimeParameter;
        cin >> pathLengthParameter;
        Car *currentCar = new Car(i,travelTimeParameter);
        for(int j = 0 ; j < pathLengthParameter ; j++){
            char connectorType;
            int index;
            int from;
            int to;

            cin >> connectorType;
            cin >> index;
            cin >> from;
            cin >> to;
            ConnectorType type ;
            switch(connectorType){
                case 'F':
                    type = ConnectorType::FERRY;
                    break;
                case 'N':
                    type = ConnectorType::NARROWBRIDGE;
                    break;
                case 'C':
                    type = ConnectorType::CROSSROAD;
                    break;
                default:
                    throw("PARSE ERROR");
                    break;
            }
            currentCar->appendToPath(index,type,from,to);
        }
        cars.push_back(currentCar);
    }
    return 0;
}
void * threadFunction(void *arguments)
{
    int *idptr = (int*)arguments;
    int id = idptr[0];
    delete idptr;

    cars[id]->takePath();
    return nullptr;
}
int main(){

    if(parse()){
        return 1;
    }

    InitWriteOutput();

    vector<pthread_t> thread_ids;
    threadCount = cars.size();
    for(int i = 0 ; i < narrowBridges.size();i++) {
        narrowBridges[i]->secondaryInitialization();
    }
    for(int i = 0 ; i < crossRoads.size();i++) {
        crossRoads[i]->secondaryInitialization();
    }
    for(int i = 0 ; i < threadCount;i++) {
        pthread_t thread_id;

        int *args = new int;
        args[0] = i;
        pthread_create(&thread_id, nullptr, threadFunction, args);
        thread_ids.push_back(thread_id);
    }
    for(int i = 0 ; i < threadCount;i++) {
        pthread_join(thread_ids[i], nullptr);
    }

    return 0;

}