#include "poisson_surface_reconstruction.h"
#include "fd_grad.h"
#include "fd_interpolate.h"
#include <igl/copyleft/marching_cubes.h>
#include <algorithm>

#include <Eigen/IterativeLinearSolvers>
#include <iostream>

template <typename T>
void print_sparse(const Eigen::SparseMatrix<T>& matrix) {
    for (int k = 0; k < matrix.outerSize(); ++k) {
        for (Eigen::SparseMatrix<double>::InnerIterator it(matrix,k); it; ++it) {
            std::cout << "M(" << it.row() << "," << it.col() << ")=" << it.value() << '\n';
        }
    }
}

void poisson_surface_reconstruction(
    const Eigen::MatrixXd & P,
    const Eigen::MatrixXd & N,
    Eigen::MatrixXd & V,
    Eigen::MatrixXi & F)
{
    ////////////////////////////////////////////////////////////////////////////
    // Construct FD grid, CONGRATULATIONS! You get this for free!
    ////////////////////////////////////////////////////////////////////////////
    // number of input points
    const int n = P.rows();
    // Grid dimensions
    int nx, ny, nz;
    // Maximum extent (side length of bounding box) of points
    double max_extent =
        (P.colwise().maxCoeff()-P.colwise().minCoeff()).maxCoeff();
    // padding: number of cells beyond bounding box of input points
    const double pad = 8;
    // choose grid spacing (h) so that shortest side gets 30+2*pad samples
    double h  = max_extent/double(30+2*pad);
    // Place bottom-left-front corner of grid at minimum of points minus padding
    Eigen::RowVector3d corner = P.colwise().minCoeff().array()-pad*h;
    // Grid dimensions should be at least 3 
    nx = std::max((P.col(0).maxCoeff()-P.col(0).minCoeff()+(2.*pad)*h)/h,3.);
    ny = std::max((P.col(1).maxCoeff()-P.col(1).minCoeff()+(2.*pad)*h)/h,3.);
    nz = std::max((P.col(2).maxCoeff()-P.col(2).minCoeff()+(2.*pad)*h)/h,3.);

    // Compute positions of grid nodes
    Eigen::MatrixXd x(nx*ny*nz, 3);
    for(int i = 0; i < nx; i++) 
    {
        for(int j = 0; j < ny; j++)
        {
            for(int k = 0; k < nz; k++)
            {
                // Convert subscript to index
                const auto ind = i + nx*(j + k * ny);
                x.row(ind) = corner + h*Eigen::RowVector3d(i,j,k);
            }
        }
    }
    Eigen::VectorXd g = Eigen::VectorXd::Zero(nx*ny*nz);

    // Distribute normal N to staggered grid values using trilinear weights Wx, Wy, Wz
    // such that we have `v` representing partial derivatives at staggered grid nodes
    Eigen::SparseMatrix<double> Wx(n, (nx-1)*ny*nz);
    Eigen::SparseMatrix<double> Wy(n, nx*(ny-1)*nz);
    Eigen::SparseMatrix<double> Wz(n, nx*ny*(nz-1));

    fd_interpolate(nx-1, ny, nz, h, corner + Eigen::RowVector3d(h/2,0,0), P, Wx);
    fd_interpolate(nx, ny-1, nz, h, corner + Eigen::RowVector3d(0,h/2,0), P, Wy);
    fd_interpolate(nx, ny, nz-1, h, corner + Eigen::RowVector3d(0,0,h/2), P, Wz);

    Eigen::VectorXd vx((nx-1)*ny*nz);
    Eigen::VectorXd vy(nx*(ny-1)*nz);
    Eigen::VectorXd vz(nx*ny*(nz-1));

    vx = Wx.transpose() * N.col(0);
    vy = Wy.transpose() * N.col(1);
    vz = Wz.transpose() * N.col(2);

    Eigen::VectorXd v((nx-1)*ny*nz + nx*(ny-1)*nz + nx*ny*(nz-1));
    v << vx,
         vy,
         vz;

    // Estimates the gradient operator `G` using finite differences
    Eigen::SparseMatrix<double> G((nx-1)*ny*nz + nx*(ny-1)*nz + nx*ny*(nz-1), nx*ny*nz);
    fd_grad(nx, ny, nz, h, G);

    // Solve a least squared problem to minimize the
    //      difference in divergence between `v` and gradient `Gg`
    Eigen::BiCGSTAB<Eigen::SparseMatrix<double>> solver;
    solver.compute(G.transpose() * G);
    g = solver.solve(G.transpose() * v);


    // Compute iso-value
    Eigen::SparseMatrix<double> W(n, nx*ny*nz);
    fd_interpolate(nx, ny, nz, h, corner, P, W);

    double sigma = (W * g).sum() / n;
    g = g.array() - sigma;

    ////////////////////////////////////////////////////////////////////////////
    // Run black box algorithm to compute mesh from implicit function: this
    // function always extracts g=0, so "pre-shift" your g values by -sigma
    ////////////////////////////////////////////////////////////////////////////
    igl::copyleft::marching_cubes(g, x, nx, ny, nz, V, F);
}


