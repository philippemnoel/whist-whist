import logging
import timber

logger = logging.getLogger(__name__)

API_KEY = "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJhdWQiOiJodHRwczovL2FwaS50aW1iZXIuaW8vIiwiZXhwIjpudWxsLCJpYXQiOjE1OTIwNjk3MTAsImlzcyI6Imh0dHBzOi8vYXBpLnRpbWJlci5pby9hcGlfa2V5cyIsInByb3ZpZGVyX2NsYWltcyI6eyJhcGlfa2V5X2lkIjo4MzIyLCJ1c2VyX2lkIjoiYXBpX2tleXw4MzIyIn0sInN1YiI6ImFwaV9rZXl8ODMyMiJ9.EW4XNnjmW21Wy4b38GWR7Tx7GHNi700-C6pHuq_3a70"
SOURCE_ID = 38863

timber_handler = timber.TimberHandler(api_key=API_KEY, source_id="test")
logger.addHandler(timber_handler)

logger.info("TIMBER??")
