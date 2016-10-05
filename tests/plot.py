#!/usr/bin/python3

import numpy as np
from matplotlib import pyplot as plt
import fnmatch
import os
import re

current_gen = 0

generation_files = {}

fig = plt.figure()
ax = fig.add_subplot(111)

def plot_generation(gen):
    global generation_files

    if gen in generation_files:
        ax.clear()

        for file in generation_files[gen]:
            x, y = np.loadtxt(file, delimiter=' ', unpack=True)
            ax.plot(x, y, 'o')

        plt.draw()


def on_key_event(event):
    global current_gen
    if event.key == 'right':
        current_gen += 1

    elif event.key == 'left':
        current_gen -= 1
        if current_gen < 0: current_gen = 0

    plot_generation(current_gen)

fig.canvas.mpl_connect('key_release_event', on_key_event)

for file in os.listdir('.'):
    match = re.search(r"pareto_([0-9]+)_([0-9]+).dat", file)
    if match:
        generation = int(match.group(1))
        pareto_id = int(match.group(2))

        if not generation in generation_files:
            generation_files[generation] = []
        generation_files[generation].append(file)

for gen in generation_files:
    generation_files[gen].sort()

plot_generation(0)

plt.show()
