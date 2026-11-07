"""Factory KB4/polar-spline lens math for offline measurement diagnostics.

This module does not select or modify a live camera calibration. A correct lens
formula alone does not establish camera identity, extrinsics or exposure poses.
No vendor binary, shader, capture or device-specific coefficients are included.
"""
from __future__ import annotations

import numpy as np

from .live_geometry import project_kb
from .measurement import unproject_kb


class PolarSplineLens:
    """KB4 projection followed by radial/tangential pixel displacements.

    This implements the five-radius, five-periodic-angle factory grid. Two zero
    radial rings precede its measured coefficients. The radial coordinate is in
    node-spacing units; angle spans five nodes per revolution. Interpolation
    clamps radial samples and wraps angular samples. Unprojection is returned
    only if it converges on the monotonic forward KB branch.

    Coordinates are factory-resolution pixel centres. Proven image sampling
    steps/offsets belong to the caller; this model applies no additional crop.
    """

    def __init__(self, specification):
        if (specification.get('cameraModel') != 'kb_polar_spline'
                or specification.get('gridSize') != [5, 5]):
            raise ValueError('Unsupported polar-spline camera model')
        try:
            self.intrinsics = np.array([specification[k] for k in ('fx', 'fy', 'cx', 'cy')], dtype=float)
            self.coefficients = np.array([specification['k'+str(i)] for i in range(1, 5)], dtype=float)
            self.spacing_pixels = float(specification['nodeSpacingR'])
            coefficients = np.array((specification['sR'], specification['sT']), dtype=float)
        except (KeyError, TypeError, ValueError) as error:
            raise ValueError('Invalid polar-spline calibration') from error
        if (self.intrinsics.shape != (4,) or self.coefficients.shape != (4,)
                or coefficients.shape != (2, 25) or not np.isfinite(coefficients).all()
                or not np.isfinite(self.intrinsics).all() or not np.isfinite(self.coefficients).all()
                or not np.isfinite(self.spacing_pixels) or self.spacing_pixels <= 0
                or np.any(self.intrinsics[:2] <= 0)):
            raise ValueError('Invalid polar-spline calibration')
        self._grid = np.zeros((7, 5, 2))
        self._grid[2:] = coefficients.reshape(2, 5, 5).transpose(1, 2, 0)
        for value in (self.intrinsics, self.coefficients, self._grid):
            value.flags.writeable = False

    @staticmethod
    def _weights(t):
        return np.stack((-.5*t+t*t-.5*t**3, 1-2.5*t*t+1.5*t**3,
                         .5*t+2*t*t-1.5*t**3, -.5*t*t+.5*t**3), axis=-1)

    def _interpolate(self, coordinates):
        base = np.floor(coordinates).astype(np.int64)
        fraction = coordinates-base
        radial = np.clip(base[:, :1]+np.arange(-1, 3), 0, 6)
        angular = (base[:, 1:]+np.arange(-1, 3)) % 5
        samples = self._grid[radial[:, :, None], angular[:, None, :]]
        return np.einsum('nr,nc,nrcd->nd', self._weights(fraction[:, 0]),
                         self._weights(fraction[:, 1]), samples)

    def pixel_shift(self, kb_pixels):
        """Return Cartesian correction for already KB-distorted pixel centres."""
        pixels = np.asarray(kb_pixels, dtype=float)
        if pixels.ndim < 1 or pixels.shape[-1] != 2 or not np.isfinite(pixels).all():
            raise ValueError('Invalid polar-spline input pixels')
        xy = pixels.reshape(-1, 2)-self.intrinsics[2:]
        radius = np.maximum(np.linalg.norm(xy, axis=-1), 1e-6)
        angle = np.arctan2(xy[:, 1], xy[:, 0])
        coordinates = np.column_stack((radius/self.spacing_pixels, (angle % (2*np.pi))*5/(2*np.pi)))
        if not np.isfinite(coordinates).all() or np.any(abs(coordinates) > 1e8):
            raise ValueError('Polar-spline coordinates exceed the arithmetic domain')
        displacement = self._interpolate(coordinates)
        c, s = np.cos(angle), np.sin(angle)
        return np.column_stack((displacement[:, 0]*c-displacement[:, 1]*s,
                                displacement[:, 0]*s+displacement[:, 1]*c)).reshape(pixels.shape)

    def project(self, rays):
        rays = np.asarray(rays, dtype=float)
        if rays.ndim < 1 or rays.shape[-1] != 3 or not np.isfinite(rays).all():
            raise ValueError('Invalid polar-spline input rays')
        kb = project_kb(rays, self.intrinsics, self.coefficients)
        corrected = kb+self.pixel_shift(kb)
        return np.where((rays[..., 2] > 0)[..., None], corrected, -100.)

    def unproject(self, pixels):
        """Return rays and explicit validity, never an unconverged inverse."""
        pixels = np.asarray(pixels, dtype=float).reshape(-1, 2)
        if not np.isfinite(pixels).all():
            raise ValueError('Invalid polar-spline inverse pixels')
        kb = pixels.copy()
        for _ in range(8):
            kb = pixels-self.pixel_shift(kb)
        rays, valid = unproject_kb(kb, self.intrinsics, self.coefficients)
        valid &= np.linalg.norm(self.project(rays)-pixels, axis=1) < 1e-6
        return rays, valid
