.. _quickstart:

Quick start 
============

.. contents::


C++
---

After following the :ref:`install` you will be able to compile and run your first C++ pagmo program:

.. _getting_started_c++:

.. literalinclude:: docs/examples/getting_started.cpp
   :language: c++
   :linenos:

Place it into a ``getting_started.cpp`` text file and compile it (for example) with:

.. code-block:: bash

   g++ -O2 -DNDEBUG -std=c++11 getting_started.cpp -pthread

If you installed pagmo with support for optional 3rd party libraries, you might need to
add additional switches to the command-line invocation of the compiler. For instance,
if you enabled the optional NLopt support, you will have to link your executable to the
``nlopt`` library:

.. code-block:: bash

   g++ -O2 -DNDEBUG -std=c++11 getting_started.cpp -pthread -lnlopt

-----------------------------------------------------------------------

Python
------

If you have successfully installed pygmo following the :ref:`install` you can try the following script:

.. _getting_started_py:

.. literalinclude:: docs/examples/getting_started.py
   :language: python
   :linenos:

Place it into a ``getting_started.py`` text file and run it with:

.. code-block:: bash

   python getting_started.py

We recommend the use of Jupyter or Ipython to enjoy pygmo the most.

