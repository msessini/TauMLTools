import gc
import glob
from DataLoaderBase import *

def LoaderThread(queue_out, queue_files, batch_size, pfCand_n, pfCand_fn,
                 output_classes, return_truth, return_weights):


    data_source = DataSource(queue_files)
    put_next = True

    while put_next:

        data = data_source.get()
        if data is None:
            break


        X_all = GetData.getdata(data.x, (batch_size, pfCand_n, pfCand_fn))
        # if np.isnan(X_all).any() or np.isinf(X_all).any():
        #     print("Nan detected X!")
        #     continue

        # if return_weights:
        #     weights = getdata(data.weight, -1)
        if return_truth:
            Y = GetData.getdata(data.y, (batch_size, output_classes))

        # if np.isnan(Y).any() or np.isinf(Y).any():
        #     print("Nan detected Y!")
        #     continue
        # if return_truth and return_weights:
        #     item = (X_all, Y, weights)
        if return_truth:
            item = (X_all, Y)
        # elif return_weights:
        #     item = (X_all, weights)
        else:
            item = X_all
        
        put_next = queue_out.put(item)

    queue_out.put_terminate()

class DataLoader (DataLoaderBase):

    def __init__(self, config, file_scaling):

        self.dataloader_core = config["Setup"]["dataloader_core"]
        self.compile_classes(config, file_scaling, self.dataloader_core)

        self.config = config

        self.input_map = {} #[pfCand_type, feature, feature_int]
        for pfCand_type in self.config["CellObjectType"]:
            self.input_map[pfCand_type] = {}
            for f_dict in self.config["Features_all"][pfCand_type]:
                f = next(iter(f_dict))
                if f not in self.config["Features_disable"][pfCand_type]:
                    self.input_map[pfCand_type][f] = \
                        getattr(getattr(R,pfCand_type+"_Features"),f)


        # global variables after compile are read out here 
        self.batch_size       = self.config["Setup"]["n_tau"]
        self.output_n         = self.config["Setup"]["output_classes"]
        self.n_load_workers   = self.config["SetupNN"]["n_load_workers"]
        self.n_batches        = self.config["SetupNN"]["n_batches"]
        self.n_batches_val    = self.config["SetupNN"]["n_batches_val"]
        self.n_batches_log    = self.config["SetupNN"]["n_batches_log"]
        self.validation_split = self.config["SetupNN"]["validation_split"]
        self.max_queue_size   = self.config["SetupNN"]["max_queue_size"]
        self.n_epochs         = self.config["SetupNN"]["n_epochs"]
        self.epoch            = self.config["SetupNN"]["epoch"]
        self.learning_rate    = self.config["SetupNN"]["learning_rate"]
        self.model_name       = self.config["SetupNN"]["model_name"]
        self.sequence_len     = self.config["SequenceLength"]

        self.setup_main       = self.config["SetupNN"]

        data_files = glob.glob(f'{self.config["Setup"]["input_dir"]}/*.root')

        self.train_files, self.val_files = \
             np.split(data_files, [int(len(data_files)*(1-self.validation_split))])

        print("Files for training:", len(self.train_files))
        print("Files for validation:", len(self.val_files))


    def get_generator(self, primary_set = True, return_truth = True, return_weights = False):

        _files = self.train_files if primary_set else self.val_files
        if len(_files)==0:
            raise RuntimeError(("Taining" if primary_set else "Validation")+\
                               " file list is empty.")

        n_batches = self.n_batches if primary_set else self.n_batches_val
        print("Number of workers in DataLoader: ", self.n_load_workers)

        def _generator():

            finish_counter = 0
            
            queue_files = mp.Queue()
            [ queue_files.put(file) for file in _files ]

            queue_out = QueueEx(max_size = self.max_queue_size, max_n_puts = n_batches)

            processes = []
            for i in range(self.n_load_workers):
                processes.append(
                mp.Process(target = LoaderThread, 
                        args = (queue_out, queue_files, self.batch_size,
                                self.sequence_len["PfCand"], len(self.input_map["PfCand"]),
                                self.output_n, return_truth, return_weights)))
                processes[-1].deamon = True
                processes[-1].start()

            while finish_counter < self.n_load_workers:
                
                item = queue_out.get()

                if isinstance(item, TerminateGenerator):
                    finish_counter+=1
                else:
                    yield item
                    
            ugly_clean(queue_files)
            queue_out.clear()

            for i, pr in enumerate(processes):
                pr.join()
            gc.collect()

        return _generator


    def get_config(self):

        '''
        At the moment get_config returns
        the config for PfCand sequence only.
        But this part is customizable
        '''
        input_shape = ( 
                        (self.batch_size, self.sequence_len["PfCand"], len(self.input_map["PfCand"])),
                        (self.batch_size, self.output_n)
                      )
        input_types = (tf.float32, tf.float32)

        return self.input_map["PfCand"], input_shape, input_types
