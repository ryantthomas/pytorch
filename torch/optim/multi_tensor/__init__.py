"""
:mod:`torch.optim.multi_tensor` is a package implementing various optimization algorithms.
Most commonly used methods are already supported, and the interface is general
enough, so that more sophisticated ones can be also easily integrated in the
future.
"""

from .adam import Adam
from .adamw import AdamW

del adam
del adamw
