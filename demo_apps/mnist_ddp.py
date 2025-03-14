# credits:
# how to use DDP module with DDP sampler: https://gist.github.com/sgraaf/5b0caa3a320f28c27c12b5efeb35aa4c
# how to setup a basic DDP example from scratch: https://pytorch.org/tutorials/intermediate/dist_tuto.html
import os
import sys
import torch
import torch.distributed as dist
import torch.multiprocessing as mp
import torch
import torch.nn as nn
import torch.nn.functional as F
import torch.optim as optim
from torchvision import datasets, transforms
import random
import numpy as np

from torch.nn.parallel import DistributedDataParallel as DDP

from torch.utils.data import DataLoader, Dataset
from torch.utils.data.distributed import DistributedSampler
import math

import time


def seed_worker(worker_id):
    worker_seed = torch.initial_seed() % 2**32
    numpy.random.seed(worker_seed)
    random.seed(worker_seed)



def get_dataset():
    train_set = datasets.MNIST('./data/MNIST/raw', train=True, download=True,
                             transform=transforms.Compose([
                                 transforms.ToTensor(),
                                 transforms.Normalize((0.1307,), (0.3081,))
                             ]))
    val_set = datasets.MNIST('./data/MNIST/raw', train=False, download=True,
                             transform=transforms.Compose([
                                 transforms.ToTensor(),
                                 transforms.Normalize((0.1307,), (0.3081,))
                             ]))

    world_size = dist.get_world_size() 
    train_sampler = DistributedSampler(train_set,num_replicas=world_size, shuffle=False)
    val_sampler = DistributedSampler(val_set,num_replicas=world_size, shuffle=False)
    batch_size = int(128 / float(world_size))
    
    g = torch.Generator()
    g.manual_seed(0)
    
    
    train_loader = DataLoader(
        dataset=train_set,
        sampler=train_sampler,
        batch_size=batch_size,
        shuffle=False,
        worker_init_fn = seed_worker,
        generator=g
        )
    val_loader = DataLoader(
        dataset=val_set,
        sampler=val_sampler,
        batch_size=batch_size,
        shuffle=False,
        worker_init_fn = seed_worker,
        generator=g
    )

    return train_loader, val_loader, batch_size
class Net(nn.Module):
    def __init__(self):
        super(Net, self).__init__()
        self.conv1 = nn.Conv2d(1, 10, kernel_size=5)
        self.conv2 = nn.Conv2d(10, 20, kernel_size=5)
        self.conv2_drop = nn.Dropout2d()
        self.fc1 = nn.Linear(320, 50)
        self.fc2 = nn.Linear(50, 10)
        self.softmax = nn.LogSoftmax(dim=1)

    def forward(self, x):
        x = F.relu(F.max_pool2d(self.conv1(x), 2))
        x = F.relu(F.max_pool2d(self.conv2_drop(self.conv2(x)), 2))
        x = x.view(-1, 320)
        x = F.relu(self.fc1(x))
        x = F.dropout(x, training=self.training)
        x = self.fc2(x)
        x = self.softmax(x)
        return x
def average_gradients(model):
    size = float(dist.get_world_size())
    for param in model.parameters():
        dist.all_reduce(param.grad.data, op=dist.ReduceOp.SUM)
        param.grad.data /= size
def reduce_dict(input_dict, average=True):
    world_size = float(dist.get_world_size())
    names, values = [], []
    for k in sorted(input_dict.keys()):
        names.append(k)
        values.append(input_dict[k])
    values = torch.stack(values, dim=0)
    dist.all_reduce(values, op=dist.ReduceOp.SUM)
    if average:
        values /= world_size
    reduced_dict = {k: v for k, v in zip(names, values)}
    return reduced_dict
def train(model,train_loader,optimizer,batch_size):
    device = torch.device(f"cuda")
    train_num_batches = int(math.ceil(len(train_loader.dataset) / float(batch_size)))
    model.train()
    # let all processes sync up before starting with a new epoch of training
    # dist.barrier()
    criterion = nn.CrossEntropyLoss().to(device)
    train_loss = 0.0
    for data, target in train_loader:
        data, target = data.to(device), target.to(device)
        optimizer.zero_grad()
        output = model(data)
        loss = criterion(output, target)
        loss.backward()
        # average gradient as DDP doesn't do it correctly
        average_gradients(model)
        optimizer.step()
        loss_ = {'loss': torch.tensor(loss.item()).to(device)}
        train_loss += reduce_dict(loss_)['loss'].item()
        # cleanup
        # dist.barrier()
        # data, target, output = data.cpu(), target.cpu(), output.cpu()
    train_loss_val = train_loss / train_num_batches
    return train_loss_val
def accuracy(output, target, topk=(1,)):
    """Computes the accuracy over the k top predictions for the specified values of k"""
    with torch.no_grad():
        maxk = max(topk)
        batch_size = target.size(0)

        _, pred = output.topk(maxk, 1, True, True)
        pred = pred.t()
        correct = pred.eq(target.view(1, -1).expand_as(pred))

        res = []
        for k in topk:
            correct_k = correct[:k].view(-1).float().sum(0, keepdim=True)
            res.append(correct_k.div_(batch_size))
        return res
def val(model, val_loader,batch_size):
    device = torch.device("cuda")
    val_num_batches = int(math.ceil(len(val_loader.dataset) / float(batch_size)))
    model.eval()
    # let all processes sync up before starting with a new epoch of training
    # dist.barrier()
    criterion = nn.CrossEntropyLoss().to(device)
    val_loss = 0.0
    with torch.no_grad():
        for data, target in val_loader:
            data, target = data.to(device), target.to(device)
            output = model(data)
            loss = criterion(output, target)
            loss_ = {'loss': torch.tensor(loss.item()).to(device)}
            val_loss += reduce_dict(loss_)['loss'].item()
    val_loss_val = val_loss / val_num_batches
    return val_loss_val
def run(rank, world_size):
    device = torch.device("cuda")
    train_loader, val_loader, batch_size = get_dataset()
    model = Net().to(device)
    model = nn.SyncBatchNorm.convert_sync_batchnorm(model) # use if model contains batchnorm.
    model = DDP(model)
    optimizer = optim.SGD(model.parameters(),lr=0.01, momentum=0.5)
    history =  {
            "rank": rank,
            "train_loss_val": [],
            "train_acc_val": [],
            "val_loss_val": [],
            "val_acc_val": []
        }
    if rank == 0:
        history = {
            "rank": rank,
            "train_loss_val": [],
            "train_acc_val": [],
            "val_loss_val": [],
            "val_acc_val": []
        }

    
    start = time.time_ns()

    for epoch in range(10):
        train_loss_val = train(model,train_loader,optimizer,batch_size)
        val_loss_val = val(model,val_loader,batch_size)
        if rank == 0:
            print(f'Rank {rank} epoch {epoch}: {train_loss_val:.2f}/{val_loss_val:.2f}')
            history['train_loss_val'].append(train_loss_val)
            history['val_loss_val'].append(val_loss_val)
    
    stop = time.time_ns()
    
    elapsed_ns = stop - start
    elapsed_sec = elapsed_ns // 1e9

    print(f'Rank {rank} finished training')
    print(history)
    
    print(f"\n\n\nRuntime: {elapsed_sec} seconds\n")
    
    cleanup(rank)  

def cleanup(rank):
    # dist.cleanup()  
    dist.destroy_process_group()
    print(f"Rank {rank} is done.")


def init_process(
        rank, # rank of the process
        world_size, # number of workers
        fn, # function to be run
        # backend='gloo',# good for single node
        # backend='nccl' # the best for CUDA
        backend='nccl'
    ):
    # information used for rank

    
    master_addr = os.environ["MASTER_ADDR"]
    master_port = os.environ["MASTER_PORT"]

    tcp_str = "tcp://" + master_addr + ":" + master_port

    dist.init_process_group(init_method=tcp_str, rank=rank, world_size=world_size)
    dist.barrier()
    fn(rank, world_size)


if __name__ == "__main__":
    world_size = int(sys.argv[1])
    rank = int(sys.argv[2])
    
    ## ensure the same exact data
    SEED = 0
    torch.manual_seed(SEED)
    np.random.seed(SEED)
    random.seed(SEED)
    torch.use_deterministic_algorithms(True)
    torch.backends.cudnn.benchmark = False


    init_process(rank, world_size, run)
