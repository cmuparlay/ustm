# PathCAS synchronization library

PathCAS is a new synchronization mechanism for concurrent data structures introduced in a PPoPP'22 paper.
- PathCAS: An Efficient Middle Ground for Concurrent Search Data Structures
- https://mc.uwaterloo.ca/pubs/pathcas/paper.pdf
- https://dl.acm.org/doi/pdf/10.1145/3503221.3508410
- The paper has a link to the code artifact for running the experiments in the paper.
- Some slides that motivate and briefly explain how to use PathCAS (from a talk Trevor gave at the University of Windsor):
  https://cs.uwaterloo.ca/~t35brown/files/ppopp22_pathcas_windsor_final.pptx

For the implementation of PathCAS
- See kcas_validate.h in this folder
- A version that is optimized using Intel's restricted transactional memory can be found in: kcas_validate_htm.h

For data structures that use PathCAS
- You can find a repo containing several here:
  https://gitlab.com/trbot86/tmbench
- Specifically, see ds/*_kcas_validate in that repository.
- A good example data structure to mimic if you'd like to use PathCAS is:
  https://gitlab.com/trbot86/tmbench/-/blob/master/ds/sigouin_int_bst_kcas_validate_htm/internal_kcas.h


