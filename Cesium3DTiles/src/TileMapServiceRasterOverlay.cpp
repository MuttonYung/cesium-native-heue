#include "Cesium3DTiles/IAssetAccessor.h"
#include "Cesium3DTiles/IAssetResponse.h"
#include "Cesium3DTiles/RasterOverlayTile.h"
#include "Cesium3DTiles/RasterOverlayTileProvider.h"
#include "Cesium3DTiles/TileMapServiceRasterOverlay.h"
#include "Cesium3DTiles/TilesetExternals.h"
#include "CesiumGeospatial/GlobeRectangle.h"
#include "CesiumGeospatial/WebMercatorProjection.h"
#include "CesiumUtility/Json.h"
#include "Uri.h"
#include <tinyxml2.h>

namespace Cesium3DTiles {

    class TileMapServiceTileProvider : public RasterOverlayTileProvider {
    public:
        TileMapServiceTileProvider(
            RasterOverlay& owner,
            const TilesetExternals& tilesetExternals,
            const CesiumGeospatial::Projection& projection,
            const CesiumGeometry::QuadtreeTilingScheme& tilingScheme,
            const CesiumGeometry::Rectangle& coverageRectangle,
            const std::string& url,
            const std::vector<IAssetAccessor::THeader>& headers,
            const std::string& fileExtension,
            uint32_t width,
            uint32_t height,
            uint32_t minimumLevel,
            uint32_t maximumLevel
        ) :
            RasterOverlayTileProvider(
                owner,
                tilesetExternals,
                projection,
                tilingScheme,
                coverageRectangle,
                minimumLevel,
                maximumLevel,
                width,
                height
            ),
            _url(url),
            _headers(headers),
            _fileExtension(fileExtension)
        {
        }

        virtual ~TileMapServiceTileProvider() {}

    protected:
        virtual std::shared_ptr<RasterOverlayTile> requestNewTile(const CesiumGeometry::QuadtreeTileID& tileID) override {
            std::string url = Uri::resolve(
                this->_url,
                std::to_string(tileID.level) + "/" +
                    std::to_string(tileID.x) + "/" +
                    std::to_string(tileID.y) +
                    this->_fileExtension,
                true
            );

            return std::make_shared<RasterOverlayTile>(this->getOwner(), tileID, this->getExternals().pAssetAccessor->requestAsset(url, this->_headers));
        }
    
    private:
        std::string _url;
        std::vector<IAssetAccessor::THeader> _headers;
        std::string _fileExtension;
    };

    TileMapServiceRasterOverlay::TileMapServiceRasterOverlay(
        const std::string& url,
        const std::vector<IAssetAccessor::THeader>& headers,
        const TileMapServiceRasterOverlayOptions& options
    ) :
        _url(url),
        _headers(headers),
        _options(options)
    {
    }

    TileMapServiceRasterOverlay::~TileMapServiceRasterOverlay() {
    }

    static std::optional<std::string> getAttributeString(const tinyxml2::XMLElement* pElement, const char* attributeName) {
        if (!pElement) {
            return std::nullopt;
        }

        const char* pAttrValue = pElement->Attribute(attributeName);
        if (!pAttrValue) {
            return std::nullopt;
        }

        return std::string(pAttrValue);
    }

    static std::optional<uint32_t> getAttributeUint32(const tinyxml2::XMLElement* pElement, const char* attributeName) {
        std::optional<std::string> s = getAttributeString(pElement, attributeName);
        if (s) {
            return std::stoul(s.value());
        }
        return std::nullopt;
    }

    static std::optional<double> getAttributeDouble(const tinyxml2::XMLElement* pElement, const char* attributeName) {
        std::optional<std::string> s = getAttributeString(pElement, attributeName);
        if (s) {
            return std::stod(s.value());
        }
        return std::nullopt;
    }

    void TileMapServiceRasterOverlay::createTileProvider(const TilesetExternals& tilesetExternals, RasterOverlay* pOwner, std::function<TileMapServiceRasterOverlay::CreateTileProviderCallback>&& callback) {
        std::string xmlUrl = Uri::resolve(this->_url, "tilemapresource.xml");
        this->_pMetadataRequest = tilesetExternals.pAssetAccessor->requestAsset(xmlUrl, this->_headers);
        this->_pMetadataRequest->bind([this, &tilesetExternals, pOwner, callback](IAssetRequest* pRequest) mutable {
            IAssetResponse* pResponse = pRequest->response();

            gsl::span<const uint8_t> data = pResponse->data();

            tinyxml2::XMLDocument doc;
            tinyxml2::XMLError error = doc.Parse(reinterpret_cast<const char*>(data.data()), data.size_bytes());
            if (error != tinyxml2::XMLError::XML_SUCCESS) {
                callback(nullptr);
                return;
            }

            tinyxml2::XMLElement* pRoot = doc.RootElement();
            if (!pRoot) {
                callback(nullptr);
                return;
            }

            std::string credit = this->_options.credit.value_or("");
            // CesiumGeospatial::Ellipsoid ellipsoid = this->_options.ellipsoid.value_or(CesiumGeospatial::Ellipsoid::WGS84);

            tinyxml2::XMLElement* pTileFormat = pRoot->FirstChildElement("TileFormat");
            std::string fileExtension = this->_options.fileExtension.value_or(getAttributeString(pTileFormat, "extension").value_or("png"));
            uint32_t tileWidth = this->_options.tileWidth.value_or(getAttributeUint32(pTileFormat, "width").value_or(256));
            uint32_t tileHeight = this->_options.tileHeight.value_or(getAttributeUint32(pTileFormat, "height").value_or(256));

            uint32_t minimumLevel = std::numeric_limits<uint32_t>::max();
            uint32_t maximumLevel = 0;

            tinyxml2::XMLElement* pTilesets = pRoot->FirstChildElement("TileSets");
            if (pTilesets) {
                tinyxml2::XMLElement* pTileset = pTilesets->FirstChildElement("TileSet");
                while (pTileset) {
                    uint32_t level = getAttributeUint32(pTileset, "order").value_or(0);
                    minimumLevel = glm::min(minimumLevel, level);
                    maximumLevel = glm::max(maximumLevel, level);

                    pTileset = pTileset->NextSiblingElement("TileSet");
                }
            }

            CesiumGeospatial::GlobeRectangle tilingSchemeRectangle = CesiumGeospatial::GeographicProjection::MAXIMUM_GLOBE_RECTANGLE;
            CesiumGeospatial::Projection projection;
            uint32_t rootTilesX = 1;
            bool isRectangleInDegrees = false;

            if (this->_options.projection) {
                projection = this->_options.projection.value();
            } else {
                std::string projectionName = getAttributeString(pTilesets, "profile").value_or("mercator");

                if (projectionName == "mercator" || projectionName == "global-mercator") {
                    projection = CesiumGeospatial::WebMercatorProjection();
                    tilingSchemeRectangle = CesiumGeospatial::WebMercatorProjection::MAXIMUM_GLOBE_RECTANGLE;

                    // Determine based on the profile attribute if this tileset was generated by gdal2tiles.py, which
                    // uses 'mercator' and 'geodetic' profiles, or by a tool compliant with the TMS standard, which is
                    // 'global-mercator' and 'global-geodetic' profiles. In the gdal2Tiles case, X and Y are always in
                    // geodetic degrees.
                    isRectangleInDegrees = projectionName.find("global-") != 0;
                } else if (projectionName == "geodetic" || projectionName == "global-geodetic") {
                    projection = CesiumGeospatial::GeographicProjection();
                    tilingSchemeRectangle = CesiumGeospatial::GeographicProjection::MAXIMUM_GLOBE_RECTANGLE;
                    rootTilesX = 2;

                    // The geodetic profile is always in degrees.
                    isRectangleInDegrees = true;
                }
            }

            minimumLevel = glm::min(minimumLevel, maximumLevel);

            minimumLevel = this->_options.minimumLevel.value_or(minimumLevel);
            maximumLevel = this->_options.maximumLevel.value_or(maximumLevel);

            CesiumGeometry::Rectangle coverageRectangle = projectRectangleSimple(projection, tilingSchemeRectangle);

            if (this->_options.coverageRectangle) {
                coverageRectangle = this->_options.coverageRectangle.value();
            } else {
                tinyxml2::XMLElement* pBoundingBox = pRoot->FirstChildElement("BoundingBox");

                std::optional<double> west = getAttributeDouble(pBoundingBox, "minx");
                std::optional<double> south = getAttributeDouble(pBoundingBox, "miny");
                std::optional<double> east = getAttributeDouble(pBoundingBox, "maxx");
                std::optional<double> north = getAttributeDouble(pBoundingBox, "maxy");

                if (west && south && east && north) {
                    if (isRectangleInDegrees) {
                        coverageRectangle = projectRectangleSimple(projection, CesiumGeospatial::GlobeRectangle::fromDegrees(west.value(), south.value(), east.value(), north.value()));
                    } else {
                        coverageRectangle = CesiumGeometry::Rectangle(west.value(), south.value(), east.value(), north.value());
                    }
                }
            }

            CesiumGeometry::QuadtreeTilingScheme tilingScheme(
                projectRectangleSimple(projection, tilingSchemeRectangle),
                rootTilesX,
                1
            );

            callback(std::make_unique<TileMapServiceTileProvider>(
                pOwner ? *pOwner: *this,
                tilesetExternals,
                projection,
                tilingScheme,
                coverageRectangle,
                this->_url,
                this->_headers,
                fileExtension.size() > 0 ? "." + fileExtension : fileExtension,
                tileWidth,
                tileHeight,
                minimumLevel,
                maximumLevel
            ));
        });
    }

}