import torch
from torch.utils.data.distributed import DistributedSampler
from torch.utils.data import DataLoader
import torch.nn as nn
import torch.optim as optim

import torchvision
import torchvision.transforms as transforms

import argparse
import os
import random
import numpy as np

import pickle
import sys
import torch.distributed as c10d
import json


def seed_worker(worker_id):
    worker_seed = torch.initial_seed() % 2**32
    numpy.random.seed(worker_seed)
    random.seed(worker_seed)

def set_random_seeds(random_seed=0):

    torch.manual_seed(random_seed)
    np.random.seed(random_seed)
    random.seed(random_seed)
    torch.use_deterministic_algorithms(True)
    torch.backends.cudnn.benchmark = False

def evaluate(model, device, test_loader):

    model.eval()

    correct = 0
    total = 0
    with torch.no_grad():
        for data in test_loader:
            images, labels = data[0].to(device), data[1].to(device)
            outputs = model(images)
            _, predicted = torch.max(outputs.data, 1)
            total += labels.size(0)
            correct += (predicted == labels).sum().item()

    accuracy = correct / total

    return accuracy

def do_train(world_size, rank):

    num_epochs_default = 10000
    batch_size_default = 256 # 1024
    learning_rate_default = 0.1
    random_seed_default = 0
    model_dir_default = "saved_models"
    model_filename_default = "resnet_distributed.pt"
    
    """
    # Each process runs on 1 GPU device specified by the local_rank argument.
    parser = argparse.ArgumentParser(formatter_class=argparse.ArgumentDefaultsHelpFormatter)
    parser.add_argument("--num_epochs", type=int, help="Number of training epochs.", default=num_epochs_default)
    parser.add_argument("--batch_size", type=int, help="Training batch size for one process.", default=batch_size_default)
    parser.add_argument("--learning_rate", type=float, help="Learning rate.", default=learning_rate_default)
    parser.add_argument("--random_seed", type=int, help="Random seed.", default=random_seed_default)
    parser.add_argument("--model_dir", type=str, help="Directory for saving models.", default=model_dir_default)
    parser.add_argument("--model_filename", type=str, help="Model filename.", default=model_filename_default)
    parser.add_argument("--resume", action="store_true", help="Resume training from saved checkpoint.")
    argv = parser.parse_args()
    """

    num_epochs = num_epochs_default
    batch_size = batch_size_default
    learning_rate = learning_rate_default
    random_seed = random_seed_default
    model_dir = model_dir_default
    model_filename = model_filename_default

    # Create directories outside the PyTorch program
    # Do not create directory here because it is not multiprocess safe
    '''
    if not os.path.exists(model_dir):
        os.makedirs(model_dir)
    '''

    model_filepath = os.path.join(model_dir, model_filename)

    # We need to use seeds to make sure that the models initialized in different processes are the same
    set_random_seeds(random_seed=random_seed)

    # Initializes the distributed backend which will take care of sychronizing nodes/GPUs
    tcp_str = "tcp://" + os.environ["MASTER_ADDR"] + ":" + os.environ["MASTER_PORT"] 
    torch.distributed.init_process_group(init_method=tcp_str, backend="nccl", rank=rank, world_size=world_size)
    torch.distributed.barrier()
    # torch.distributed.init_process_group(backend="gloo")

    #pg = c10d.distributed_c10d._get_default_group()
    #pg._enable_collectives_timing()

    # Encapsulate the model on the GPU assigned to the current process
    model = torchvision.models.resnet18(pretrained=False)

    device = torch.device("cuda:0")
    model = model.to(device)
    ddp_model = torch.nn.parallel.DistributedDataParallel(model)

    # Prepare dataset and dataloader
    transform = transforms.Compose([
        transforms.RandomCrop(32, padding=4),
        transforms.RandomHorizontalFlip(),
        transforms.ToTensor(),
        transforms.Normalize((0.4914, 0.4822, 0.4465), (0.2023, 0.1994, 0.2010)),
    ])

    # Data should be prefetched
    # Download should be set to be False, because it is not multiprocess safe
    train_set = torchvision.datasets.CIFAR10(root="data", train=True, download=True, transform=transform) 
    test_set = torchvision.datasets.CIFAR10(root="data", train=False, download=True, transform=transform)

    # Restricts data loading to a subset of the dataset exclusive to the current process
    train_sampler = DistributedSampler(dataset=train_set, shuffle=False)
    
    g = torch.Generator()
    g.manual_seed(0)

    train_loader = DataLoader(dataset=train_set, batch_size=batch_size, sampler=train_sampler, num_workers=8, shuffle=False, work_init_fn=seed_worker, generator=g)
    # Test loader does not have to follow distributed sampling strategy
    test_loader = DataLoader(dataset=test_set, batch_size=128, shuffle=False, num_workers=8, worker_init_fn=seed_worker, generator=g)

    criterion = nn.CrossEntropyLoss()
    optimizer = optim.SGD(ddp_model.parameters(), lr=learning_rate, momentum=0.9, weight_decay=1e-5)
    

    # Loop over the dataset multiple times
    for epoch in range(num_epochs):

        

        print("Rank: {}, Epoch: {}, Training ...".format(rank, epoch))
        
        # Save and evaluate model routinely
        if epoch % 1 == 0:
            if rank == 0:
                accuracy = evaluate(model=ddp_model, device=device, test_loader=test_loader)
                print("-" * 75)
                print("Epoch: {}, Accuracy: {}".format(epoch, accuracy))
                print("-" * 75)
            
            """
            trace_filename = "timing_dumps/rank_" + str(rank) + "_epoch_" + str(epoch) + "_nccl_trace.json"
            with open(trace_filename, "w") as output_file:
                print("Dumping output nccl trace...")
                t = pickle.loads(torch._C._distributed_c10d._dump_nccl_trace())
                json.dump(t, output_file, ensure_ascii=False)
            """

        if epoch == 10:
            break

        ddp_model.train()

        for data in train_loader:
            inputs, labels = data[0].to(device), data[1].to(device)
            optimizer.zero_grad()
            outputs = ddp_model(inputs)
            loss = criterion(outputs, labels)
            loss.backward()
            optimizer.step()

if __name__ == "__main__":
    if len(sys.argv) != 3:
        print("Error: expected usage resnet_ddp.py <world_size> <rank>")
        sys.exit(1)

    world_size = int(sys.argv[1])
    rank = int(sys.argv[2])
    do_train(world_size, rank)
