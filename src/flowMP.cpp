// Default functional placeholders
//void blankz(){}
//std::function<void()> blankTemp = blankz;

#include "flowMP.h"

// Constructor
Worker::Worker(std::function<voidvec(voidvec,int*)> ProcessData,
               int numInputs, int numOutputs,
               std::function<void()> FunctionInit, //= blankTemp
               std::function<void()> FunctionCleanup,
               std::string BlockName ) //= blankTemp
{
        m_ProcessData = ProcessData;
        m_FunctionInit = FunctionInit;
        m_FunctionCleanup = FunctionCleanup;

        // Set Parameters
        m_StopThread = false;
        m_NumInputs = numInputs;
        m_NumOutputs = numOutputs;
        m_ProcessData = ProcessData;
        m_FunctionInit = FunctionInit;
        m_FunctionCleanup = FunctionCleanup;
        m_BlockName = BlockName;

        //Setup Ports
        for(int port=0; port<numInputs; port++)
        {
                boost::shared_ptr<std::atomic<int> > sptr (new std::atomic<int>(0));
                m_InputQueueSizes.push_back(sptr);
                boost::shared_ptr<boost::mutex> sptr2 (new boost::mutex);
                m_InputMutexs.push_back(sptr2);
                boost::shared_ptr<boost::condition_variable> sptr3 (new boost::condition_variable);
                m_InputConds.push_back(sptr3);
                boost::shared_ptr<std::queue<void*> > sptr4 (new std::queue<void*>);
                m_InputQueues.push_back(sptr4);
        }
        for(int port=0; port<numOutputs; port++)
        {
                boost::shared_ptr<std::atomic<int> > sptr (new std::atomic<int>(0));
                m_OutputQueueSizes.push_back(sptr);
                boost::shared_ptr<boost::mutex> sptr2 (new boost::mutex);
                m_OutputMutexs.push_back(sptr2);
                boost::shared_ptr<boost::condition_variable> sptr3 (new boost::condition_variable);
                m_OutputConds.push_back(sptr3);
                boost::shared_ptr<std::queue<void*> > sptr4 (new std::queue<void*>);
                m_OutputQueues.push_back(sptr4);
        }

}
/////////////////////////////////////////////////////
///////// INPUTS
/////////////////////////////////////////////////////
voidvec Worker::readFromInputQueues()
{
        voidvec inputs(m_NumInputs);
        // Cycle over ports
        for(int inport=0; inport < m_NumInputs; inport++)
        {
                inputs[inport] = readFromInputQueue(inport);
                // Stop thread
                if (m_StopThread)
                        break;
        }

        return inputs;

}
// Get data from queue
void* Worker::readFromInputQueue(int inport)
{

        // Wait for data
        if (*(m_InputQueueSizes[inport]) == 0) {
                // Wait for signal
                //boost::unique_lock<boost::mutex> lock(*(m_InputMutexs[inport]));
                boost::mutex::scoped_lock lock(*(m_InputMutexs[inport]));

                do {(*(m_InputConds[inport])).wait(lock); } //Wait will tell the lock to unlock
                while ((*(m_InputQueueSizes[inport]) == 0) && (!m_StopThread)); //If we wake up, make sure we have data to work with
        }
        // else if ((*(m_InputQueueSizes[inport]))>WARNINGQSIZE)
        //     std::cout<<"Input Queue Size: "<<(*(m_InputQueueSizes[inport]))<<" | "<< m_BlockName << std::endl;

        // Thread stopped
        if (m_StopThread)
        {
                void* empty;
                return empty;
        }

        // When data is ready read it off queue
        //boost::lock_guard<boost::mutex> lock(*(m_InputMutexs[inport]));
        boost::mutex::scoped_lock lock(*(m_InputMutexs[inport]));

        void* data = (*(m_InputQueues[inport])).front();
        (*(m_InputQueues[inport])).pop();

        // Update queue size atomic
        (*(m_InputQueueSizes[inport]))--;

        return data;
}

/////////////////////////////////////////////////////
///////// OUTPUTS
/////////////////////////////////////////////////////
void Worker::addToOutputQueues(voidvec ProcessedDataVector)
{
        // Cycle over ports
        for(int outport=0; outport < m_NumOutputs; outport++)
                addToOutputQueue(ProcessedDataVector[outport],outport);
}

// Output data to next block
void Worker::addToOutputQueue(void* processedData, int outport)
{
        // Add data to queue
        boost::lock_guard<boost::mutex> lock(*(m_OutputMutexs[outport]));
        (*(m_OutputQueues[outport])).push(processedData);
        (*(m_OutputQueueSizes[outport]))++;

        // Notify next block
        (*(m_OutputConds[outport])).notify_one();

}

// Notify all connected blocks (this being a source to them)
void Worker::notifyConnectedBlocks()
{
        // Cycle over ports
        for(int outport=0; outport < m_NumOutputs; outport++)
                (*(m_OutputConds[outport])).notify_one();
}

// In and Out Process Block
void Worker::block()
{
    #ifdef UNIX
        prctl(PR_SET_NAME,m_BlockName.c_str(),0,0,0);
    #endif
        voidvec data;
        voidvec processedData;
        int flag = 0;

        // Initialize threaded function
        m_FunctionInit();

        while (!m_StopThread)
        {
                //Read Data
                data = readFromInputQueues();
                if (m_StopThread)
                        break;

                //Process Data
                processedData = m_ProcessData(data,&flag);
                //Output Data
                if (flag>0)
                        addToOutputQueues(processedData);
        }
        // Cleanup after threaded function
        m_FunctionCleanup();

        // Notify when thread completes
        // std::cout << "Thread Done: " << m_BlockName << " ID: " << boost::this_thread::get_id() << '\n';

}

// Source Block
void Worker::block_source()
{
    #ifdef UNIX
        prctl(PR_SET_NAME,m_BlockName.c_str(),0,0,0);
    #endif

        // Delay startup, to make sure all blocks have started
        int usec = 1000;
        boost::this_thread::sleep(boost::posix_time::microseconds(usec));

        voidvec data;
        voidvec processedData;
        int flag = 0;

        // Initialize threaded function
        m_FunctionInit();

        while (!m_StopThread)
        {
                //Process Data
                processedData = m_ProcessData(data, &flag);
                if (m_StopThread)
                        break;

                //Output Data
                if (flag>0)
                        addToOutputQueues(processedData);

        }
        std::cout<<"Source block finished\n";

        // Cleanup after threaded function
        m_FunctionCleanup();

        // Notify when thread completes
        // std::cout << "Thread Done: " << m_BlockName << " ID: " << boost::this_thread::get_id() << '\n';

}
// Sink Block
void Worker::block_sink()
{
    #ifdef UNIX
        prctl(PR_SET_NAME,m_BlockName.c_str(),0,0,0);
    #endif

        voidvec data;
        voidvec processedData;
        int flag = 0;

        // Initialize threaded function
        m_FunctionInit();

        while (!m_StopThread)
        {
                //Read Data
                data = readFromInputQueues();
                if (m_StopThread)
                        break;

                //Process Data
                processedData = m_ProcessData(data,&flag);
        }
        // std::cout<<"Sink block finished\n";

        // Cleanup after threaded function
        m_FunctionCleanup();

        // Notify when thread completes
        // std::cout << "Thread Done: " << m_BlockName << " ID: " << boost::this_thread::get_id() << '\n';

}
// Spawn threads for given block type
void Worker::run()
{
        boost::thread m_BlockThread(&Worker::block, this);
}
void Worker::run_source()
{
        boost::thread m_BlockThread(&Worker::block_source, this);
}
void Worker::run_sink()
{
        boost::thread m_BlockThread(&Worker::block_sink, this);
}

// Connect blocks together
void connect(Worker &aBlock, int outport, Worker &bBlock, int inport)
{
	// Map mutexes
	bBlock.m_InputMutexs[inport] = aBlock.m_OutputMutexs[outport];
	// Map conditionals
	bBlock.m_InputConds[inport] = aBlock.m_OutputConds[outport];
	// Map queues
	bBlock.m_InputQueues[inport] = aBlock.m_OutputQueues[outport];
	// Map atomics
	bBlock.m_InputQueueSizes[inport] = aBlock.m_OutputQueueSizes[outport];
}